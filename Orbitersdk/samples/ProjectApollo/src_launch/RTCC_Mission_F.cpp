/****************************************************************************
This file is part of Project Apollo - NASSP
Copyright 2018

RTCC Calculations for Mission F

Project Apollo is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

Project Apollo is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Project Apollo; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

See http://nassp.sourceforge.net/license/ for more details.

**************************************************************************/

#include "Orbitersdk.h"
#include "soundlib.h"
#include "apolloguidance.h"
#include "saturn.h"
#include "saturnv.h"
#include "LVDC.h"
#include "iu.h"
#include "LEM.h"
#include "../src_rtccmfd/OrbMech.h"
#include "mcc.h"
#include "rtcc.h"

bool RTCC::CalculationMTP_F(int fcn, LPVOID &pad, char * upString, char * upDesc, char * upMessage)
{
	char uplinkdata[1024 * 3];
	bool preliminary = false;
	bool scrubbed = false;

	//Hardcoded for now, better solution at some point...
	double LSAzi = -91.0*RAD;

	switch (fcn) {
	case 1: //MISSION INITIALIZATION GROUND LIFTOFF TIME UPDATE
	{
		char Buff[128];

		//P80 MED: mission initialization
		mcc->mcc_calcs.PrelaunchMissionInitialization();

		//P10 MED: Enter actual liftoff time
		double tephem_scal;
		Saturn *cm = (Saturn *)calcParams.src;

		//Get TEPHEM
		tephem_scal = GetTEPHEMFromAGC(&cm->agc.vagc, true);
		double LaunchMJD = (tephem_scal / 8640000.) + SystemParameters.TEPHEM0;
		LaunchMJD = (LaunchMJD - SystemParameters.GMTBASE)*24.0;

		int hh, mm;
		double ss;

		OrbMech::SStoHHMMSS(LaunchMJD*3600.0, hh, mm, ss, 0.01);

		sprintf_s(Buff, "P10,CSM,%d:%d:%.2lf;", hh, mm, ss);
		GMGMED(Buff);

		//P12: CSM GRR and Azimuth
		SaturnV *SatV = (SaturnV*)cm;
		LVDCSV *lvdc = (LVDCSV*)SatV->iu->GetLVDC();
		double Azi = lvdc->Azimuth*DEG;
		double T_GRR = lvdc->T_L;

		sprintf_s(Buff, "P12,CSM,%d:%d:%.2lf,%.2lf;", hh, mm, ss, Azi);
		GMGMED(Buff);

		//P15: CMC, LGC and AGS clock zero
		sprintf_s(Buff, "P15,AGC,%d:%d:%.2lf;", hh, mm, ss);
		GMGMED(Buff);
		sprintf_s(Buff, "P15,LGC,%d:%d:%.2lf;", hh, mm, ss);
		GMGMED(Buff);
		GMGMED("P15,AGS,,90:00:00;");

		//P12: IU GRR and Azimuth
		OrbMech::SStoHHMMSS(T_GRR, hh, mm, ss, 0.01);
		sprintf_s(Buff, "P12,IU1,%d:%d:%.2lf,%.2lf;", hh, mm, ss, Azi);
		GMGMED(Buff);

		//Get actual liftoff REFSMMAT from telemetry
		BZSTLM.CMC_REFSMMAT = GetREFSMMATfromAGC(&mcc->cm->agc.vagc, true);
		BZSTLM.CMCRefsPresent = true;
		EMSGSUPP(1, 1);
		//Make telemetry matrix current
		GMGMED("G00,CSM,TLM,CSM,CUR;");

		//F62: Interpolate SFP
		sprintf_s(Buff, "F62,,1,%.3lf;", Azi);
		GMGMED(Buff);
	}
	break;
	case 2: //TLI SIMULATION
	{
		if (PZMPTCSM.ManeuverNum > 0)
		{
			//Delete maneuvers from MPT
			GMGMED("M62,CSM,1,D;");
		}

		//Config and mass update
		med_m55.Table = RTCC_MPT_CSM;
		MPTMassUpdate(calcParams.src, med_m50, med_m55, med_m49);
		PMMWTC(55);
		med_m50.Table = RTCC_MPT_CSM;
		med_m50.WeightGET = GETfromGMT(RTCCPresentTimeGMT());
		PMMWTC(50);

		//Trajectory Update
		StateVectorTableEntry sv0;
		sv0.Vector = StateVectorCalcEphem(calcParams.src);
		sv0.LandingSiteIndicator = false;
		sv0.VectorCode = "APIC001";

		PMSVCT(4, RTCC_MPT_CSM, sv0);

		//Add TLI to MPT
		if (mcc->mcc_calcs.GETEval(3.0*3600.0))
		{
			//Second opportunity
			GMGMED("M68,CSM,2;");
		}
		else
		{
			//First opportunity
			GMGMED("M68,CSM,1;");
		}

		//Add separation maneuver to MPT
		GMGMED("M40,P1,0.0;");
		med_m66.Table = RTCC_MPT_CSM;
		med_m66.ReplaceCode = 0; //Don't replace
		med_m66.GETBI = GETfromGMT(PZMPTCSM.mantable[0].GMT_BO) + 15.0*60.0;
		med_m66.Thruster = RTCC_ENGINETYPE_CSMRCSPLUS4;
		med_m66.AttitudeOpt = RTCC_ATTITUDE_INERTIAL;
		med_m66.BurnParamNo = 1;
		med_m66.CoordInd = 0; //LVLH
		med_m66.Att = _V(41.6*RAD, 120.8*RAD, 131.9*RAD); //Make this launch day specific?
		med_m66.ConfigChangeInd = RTCC_CONFIGCHANGE_UNDOCKING;
		med_m66.FinalConfig = "CL";

		//Dummy data
		std::vector<std::string> str;
		PMMMED("66", str);

		//Save TLI time
		TimeofIgnition = GETfromGMT(PZMPTCSM.mantable[0].GMT_BI);
		calcParams.TLI = GETfromGMT(PZMPTCSM.mantable[0].GMT_BO);
	}
	break;
	case 3: //TLI+90 PAD + State Vector
	{
		EntryOpt entopt;
		EntryResults res;
		AP11ManPADOpt opt;
		double GETbase, TLIBase, TIG, GMTSV;
		EphemerisData sv, sv_uplink;
		SV sv1;
		char buffer1[1000];

		AP11MNV * form = (AP11MNV *)pad;

		GETbase = CalcGETBase();
		sv = StateVectorCalcEphem(calcParams.src);

		TLIBase = floor((TimeofIgnition / 1800.0) + 0.5)*1800.0; //Round to next half hour
		TIG = TLIBase + 90.0*60.0;
		entopt.lng = -25.0*RAD;

		sv1.mass = PZMPTCSM.mantable[1].CommonBlock.CSMMass;
		sv1.gravref = hEarth;
		sv1.MJD = OrbMech::MJDfromGET(PZMPTCSM.mantable[1].GMT_BO, SystemParameters.GMTBASE);
		sv1.R = PZMPTCSM.mantable[1].R_BO;
		sv1.V = PZMPTCSM.mantable[1].V_BO;

		entopt.entrylongmanual = true;
		entopt.enginetype = RTCC_ENGINETYPE_CSMSPS;
		entopt.TIGguess = TIG;
		entopt.t_Z = OrbMech::HHMMSSToSS(13.0, 0.0, 0.0);
		entopt.type = 1;
		entopt.vessel = calcParams.src;
		entopt.RV_MCC = sv1;

		EntryTargeting(&entopt, &res); //Target Load for uplink

		opt.TIG = res.P30TIG;
		opt.dV_LVLH = res.dV_LVLH;
		opt.enginetype = RTCC_ENGINETYPE_CSMSPS;
		opt.HeadsUp = true;
		opt.REFSMMAT = GetREFSMMATfromAGC(&mcc->cm->agc.vagc, true);
		opt.RV_MCC = ConvertSVtoEphemData(sv1);
		opt.WeightsTable.CC[RTCC_CONFIG_C] = true;
		opt.WeightsTable.ConfigWeight = opt.WeightsTable.CSMWeight = sv1.mass;

		AP11ManeuverPAD(opt, *form);

		form->lat = res.latitude*DEG;
		form->lng = res.longitude*DEG;
		form->RTGO = res.RTGO;
		form->VI0 = res.VIO / 0.3048;
		form->GET05G = res.GET05G;

		sprintf(form->purpose, "TLI+90");
		sprintf(form->remarks, "No ullage");

		GMTSV = PZMPTCSM.TimeToBeginManeuver[0] - 10.0*60.0; //10 minutes before TB6
		sv_uplink = coast(sv, GMTSV - sv.GMT, RTCC_MPT_CSM); //Coast with venting and drag taken into account

		AGCStateVectorUpdate(buffer1, RTCC_MPT_CSM, RTCC_MPT_CSM, sv_uplink, true);

		sprintf(uplinkdata, "%s", buffer1);
		if (upString != NULL) {
			// give to mcc
			strncpy(upString, uplinkdata, 1024 * 3);
			sprintf(upDesc, "CSM state vector and V66");
		}
	}
	break;
	case 4: //TLI+4 PAD
	{
		AP11BLKOpt opt;
		SV sv1;

		P37PAD * form = (P37PAD *)pad;

		sv1.mass = PZMPTCSM.mantable[1].CommonBlock.CSMMass;
		sv1.gravref = hEarth;
		sv1.MJD = OrbMech::MJDfromGET(PZMPTCSM.mantable[1].GMT_BO, SystemParameters.GMTBASE);
		sv1.R = PZMPTCSM.mantable[1].R_BO;
		sv1.V = PZMPTCSM.mantable[1].V_BO;

		opt.n = 1;

		opt.GETI.push_back(OrbMech::HHMMSSToSS(6, 30, 0));
		opt.T_Z.push_back(OrbMech::HHMMSSToSS(22, 21, 0));
		opt.lng.push_back(-165.0*RAD);
		opt.useSV = true;
		opt.RV_MCC = sv1;

		AP11BlockData(&opt, *form);
	}
	break;
	case 5: //TLI PAD
	{
		SV sv;

		TLIPAD * form = (TLIPAD *)pad;

		//DMT for TLI
		GMGMED("U20,CSM,1;");

		form->TB6P = DMTBuffer[0].GETI - 9.0*60.0 - 38.0;
		form->IgnATT = DMTBuffer[0].IMUAtt;
		form->BurnTime = DMTBuffer[0].DT_B;
		form->dVC = DMTBuffer[0].DVC;
		form->VI = length(PZMPTCSM.mantable[0].V_BO) / 0.3048;
		form->type = 2;

		//DMT for sep maneuver
		GMGMED("U20,CSM,2;");
		form->SepATT = DMTBuffer[0].IMUAtt;
		form->ExtATT = OrbMech::imulimit(_V(300.0 - form->SepATT.x, form->SepATT.y + 180.0, 360.0 - form->SepATT.z));

		//Delete maneuvers from MPT
		GMGMED("M62,CSM,1,D;");

		//Set anchor vector time to 0, so that no trajectory updates are done anymore
		EZANCHR1.AnchorVectors[9].Vector.GMT = 0.0;
	}
	break;
	case 6: //TLI Evaluation
	{
		SaturnV *SatV = (SaturnV*)calcParams.src;
		LVDCSV *lvdc = (LVDCSV*)SatV->iu->GetLVDC();

		if (lvdc->LVDC_Timebase == 5)
		{
			scrubbed = true;
		}
	}
	break;
	case 7: //Evasive Maneuver
	{
		AP11ManPADOpt opt;

		AP11MNV * form = (AP11MNV *)pad;

		opt.TIG = calcParams.TLI + 2.0*3600.0;
		opt.dV_LVLH = _V(5.1, 0.0, 19.0)*0.3048;
		opt.enginetype = RTCC_ENGINETYPE_CSMSPS;
		opt.HeadsUp = true;
		opt.REFSMMAT = GetREFSMMATfromAGC(&mcc->cm->agc.vagc, true);
		opt.RV_MCC = StateVectorCalcEphem(calcParams.src);
		opt.WeightsTable = GetWeightsTable(calcParams.src, true, true);

		AP11ManeuverPAD(opt, *form);

		sprintf(form->purpose, "Evasive");
		sprintf(form->remarks, "No ullage");
	}
	break;
	case 8: //Block Data 1
	case 9: //Block Data 2
	{
		AP11BLKOpt opt;

		P37PAD * form = (P37PAD *)pad;

		double TLIbase = floor((calcParams.TLI / 1800.0) + 0.5)*1800.0; //Round to next half hour

		if (fcn == 8)
		{
			opt.n = 3;

			opt.GETI.push_back(TLIbase + 11.0*3600.0);
			opt.T_Z.push_back(OrbMech::HHMMSSToSS(46, 37, 0));
			opt.lng.push_back(-165.0*RAD);
			opt.GETI.push_back(TLIbase + 25.0*3600.0);
			opt.T_Z.push_back(OrbMech::HHMMSSToSS(70, 28, 0));
			opt.lng.push_back(-165.0*RAD);
			opt.GETI.push_back(TLIbase + 35.0*3600.0);
			opt.T_Z.push_back(OrbMech::HHMMSSToSS(94, 35, 0));
			opt.lng.push_back(-165.0*RAD);
		}
		else
		{
			opt.n = 4;

			opt.GETI.push_back(TLIbase + 25.0*3600.0);
			opt.T_Z.push_back(OrbMech::HHMMSSToSS(70, 28, 0));
			opt.lng.push_back(-165.0*RAD);
			opt.GETI.push_back(TLIbase + 35.0*3600.0);
			opt.T_Z.push_back(OrbMech::HHMMSSToSS(94, 35, 0));
			opt.lng.push_back(-165.0*RAD);
			opt.GETI.push_back(TLIbase + 44.0*3600.0);
			opt.T_Z.push_back(OrbMech::HHMMSSToSS(94, 14, 0));
			opt.lng.push_back(-165.0*RAD);
			opt.GETI.push_back(TLIbase + 53.0*3600.0);
			opt.T_Z.push_back(OrbMech::HHMMSSToSS(118, 33, 0));
			opt.lng.push_back(-165.0*RAD);
		}

		AP11BlockData(&opt, *form);
	}
	break;
	case 10: //PTC REFSMMAT
	{
		REFSMMATOpt refsopt;
		MATRIX3 REFSMMAT;
		char buffer1[1000];

		refsopt.REFSMMATopt = 6;
		refsopt.REFSMMATTime = 40365.25560140741; //133:19:04 GET of nominal mission

		REFSMMAT = REFSMMATCalc(&refsopt);

		AGCDesiredREFSMMATUpdate(buffer1, REFSMMAT, true);
		sprintf(uplinkdata, "%s", buffer1);

		if (upString != NULL) {
			// give to mcc
			strncpy(upString, uplinkdata, 1024 * 3);
			sprintf(upDesc, "PTC REFSMMAT");
		}
	}
	break;
	case 11: //MCC-1 EVALUATION
	case 12: //MCC-1
	case 13: //MCC-2
	{
		AP11ManPADOpt manopt;
		double GETbase, P30TIG, MCC1GET, MCC2GET, MCC3GET, CSMmass, LMmass;
		int engine;
		VECTOR3 dV_LVLH;
		EphemerisData sv;
		char manname[8];

		if (fcn == 11 || fcn == 12)
		{
			sprintf(manname, "MCC1");
		}
		else if (fcn == 13)
		{
			sprintf(manname, "MCC2");
		}

		if (calcParams.LOI == 0)
		{
			calcParams.LOI = OrbMech::HHMMSSToSS(75.0, 49.0, 40.2);
		}

		double TLIbase = floor((TimeofIgnition / 60.0) + 0.5)*60.0; //Round TLI ignition time to next minute

		MCC1GET = TLIbase + 9.0*3600.0;
		MCC2GET = TLIbase + 24.0*3600.0;
		MCC3GET = calcParams.LOI - 22.0*3600.0;

		AP11MNV * form = (AP11MNV *)pad;
		GETbase = CalcGETBase();

		sv = StateVectorCalcEphem(calcParams.src); //State vector for uplink

		PZMCCPLN.MidcourseGET = MCC3GET;
		PZMCCPLN.Config = true;
		PZMCCPLN.Column = 1;
		PZMCCPLN.SFPBlockNum = 1;
		PZMCCPLN.Mode = 3;
		CSMmass = calcParams.src->GetMass();
		LMmass = calcParams.tgt->GetMass();

		TranslunarMidcourseCorrectionProcessor(sv, CSMmass, LMmass);

		//For the MCC-1 evaluation store new targeting data, so even if we skip MCC-1 and MCC-2 these numbers are generated
		if (fcn == 11)
		{
			//Store new LOI time
			calcParams.LOI = PZMCCDIS.data[0].GET_LOI;
			//Transfer MCC plan to skeleton flight plan table
			GMGMED("F30,1;");
		}

		if (length(PZMCCDIS.data[0].DV_MCC) < 25.0*0.3048)
		{
			scrubbed = true;
		}
		else
		{
			if (fcn == 11 || fcn == 12)
			{
				PZMCCPLN.MidcourseGET = MCC1GET;
			}
			else
			{
				PZMCCPLN.MidcourseGET = MCC2GET;
			}

			TranslunarMidcourseCorrectionProcessor(sv, CSMmass, LMmass);

			//Transfer MCC plan to skeleton flight plan table
			GMGMED("F30,1;");

			//Scrub MCC-1 if DV is less than 50 ft/s
			if (fcn == 11 && length(PZMCCDIS.data[0].DV_MCC) < 50.0*0.3048)
			{
				scrubbed = true;
			}
			else
			{
				calcParams.LOI = PZMCCDIS.data[0].GET_LOI;
				engine = mcc->mcc_calcs.SPSRCSDecision(SPS_THRUST / calcParams.src->GetMass(), PZMCCDIS.data[0].DV_MCC);
				PoweredFlightProcessor(sv, CSMmass, PZMCCPLN.MidcourseGET, engine, LMmass, PZMCCXFR.V_man_after[0] - PZMCCXFR.sv_man_bef[0].V, false, P30TIG, dV_LVLH);
			}
		}

		//MCC-1 Evaluation
		if (fcn == 11)
		{
			if (scrubbed)
			{
				sprintf(upMessage, "%s has been scrubbed.", manname);
			}
			else
			{
				sprintf(upMessage, "%s with liftoff REFSMMAT.", manname);
			}
		}
		//MCC-1 and MCC-2 Update
		else
		{
			if (scrubbed)
			{
				char buffer1[1000];

				sprintf(upMessage, "%s has been scrubbed.", manname);
				sprintf(upDesc, "CSM state vector and V66");

				AGCStateVectorUpdate(buffer1, RTCC_MPT_CSM, RTCC_MPT_CSM, sv, true);

				sprintf(uplinkdata, "%s", buffer1);
				if (upString != NULL) {
					// give to mcc
					strncpy(upString, uplinkdata, 1024 * 3);
				}
			}
			else
			{
				char buffer1[1000];
				char buffer2[1000];

				manopt.WeightsTable = GetWeightsTable(calcParams.src, true, true);
				manopt.TIG = P30TIG;
				manopt.dV_LVLH = dV_LVLH;
				manopt.enginetype = mcc->mcc_calcs.SPSRCSDecision(SPS_THRUST / manopt.WeightsTable.ConfigWeight, dV_LVLH);
				manopt.HeadsUp = true;
				manopt.REFSMMAT = GetREFSMMATfromAGC(&mcc->cm->agc.vagc, true);
				manopt.RV_MCC = sv;

				AP11ManeuverPAD(manopt, *form);
				sprintf(form->purpose, manname);
				if (manopt.enginetype == RTCC_ENGINETYPE_CSMSPS) sprintf(form->remarks, "No ullage");

				AGCStateVectorUpdate(buffer1, RTCC_MPT_CSM, RTCC_MPT_CSM, sv, true);
				CMCExternalDeltaVUpdate(buffer2, P30TIG, dV_LVLH);

				sprintf(uplinkdata, "%s%s", buffer1, buffer2);
				if (upString != NULL) {
					// give to mcc
					strncpy(upString, uplinkdata, 1024 * 3);
					sprintf(upDesc, "CSM state vector and V66, target load");
				}
			}
		}
	}
	break;
	case 14: //Lunar Flyby PAD
	{
		RTEMoonOpt entopt;
		EntryResults res;
		AP11ManPADOpt opt;
		SV sv;
		char buffer1[1000];

		AP11MNV * form = (AP11MNV *)pad;

		sv = StateVectorCalc(calcParams.src); //State vector for uplink

		entopt.EntryLng = -165.0*RAD;
		entopt.returnspeed = 0;
		entopt.SMODE = 14;
		entopt.RV_MCC = sv;
		entopt.TIGguess = calcParams.LOI - 5.0*3600.0;
		entopt.vessel = calcParams.src;

		RTEMoonTargeting(&entopt, &res);

		opt.TIG = res.P30TIG;
		opt.dV_LVLH = res.dV_LVLH;
		opt.enginetype = RTCC_ENGINETYPE_CSMSPS;
		opt.HeadsUp = false;
		opt.REFSMMAT = GetREFSMMATfromAGC(&mcc->cm->agc.vagc, true);
		opt.RV_MCC = ConvertSVtoEphemData(sv);
		opt.WeightsTable = GetWeightsTable(calcParams.src, true, true);

		AP11ManeuverPAD(opt, *form);

		sprintf(form->purpose, "Flyby");
		sprintf(form->remarks, "No ullage. Height of pericynthion is %.0f NM", res.FlybyAlt / 1852.0);
		form->lat = res.latitude*DEG;
		form->lng = res.longitude*DEG;
		form->RTGO = res.RTGO;
		form->VI0 = res.VIO / 0.3048;
		form->GET05G = res.GET05G;

		//Save parameters for further use
		SplashLatitude = res.latitude;
		SplashLongitude = res.longitude;
		calcParams.TEI = res.P30TIG;
		calcParams.EI = res.GET400K;

		AGCStateVectorUpdate(buffer1, sv, true, true);

		sprintf(uplinkdata, "%s", buffer1);
		if (upString != NULL) {
			// give to mcc
			strncpy(upString, uplinkdata, 1024 * 3);
			sprintf(upDesc, "CSM state vector and V66");
		}
	}
	break;
	case 15: //MCC-3
	{
		AP11ManPADOpt manopt;
		VECTOR3 dV_LVLH, dv;
		EphemerisData sv_ephem;
		PLAWDTOutput WeightsTable;
		double P30TIG, tig;
		int engine, loisol;

		AP11MNV * form = (AP11MNV *)pad;

		sv_ephem = StateVectorCalcEphem(calcParams.src);
		WeightsTable = GetWeightsTable(calcParams.src, true, true);

		//Calculate MCC-3
		PZMCCPLN.MidcourseGET = calcParams.LOI - 22.0*3600.0;
		PZMCCPLN.Config = true;
		PZMCCPLN.Column = 1;
		PZMCCPLN.SFPBlockNum = 2;
		PZMCCPLN.Mode = 1;

		TranslunarMidcourseCorrectionProcessor(sv_ephem, WeightsTable.CSMWeight, WeightsTable.LMAscWeight + WeightsTable.LMDscWeight);

		tig = GETfromGMT(PZMCCXFR.sv_man_bef[0].GMT);
		dv = PZMCCXFR.V_man_after[0] - PZMCCXFR.sv_man_bef[0].V;

		//DV smaller than 3 ft/s?
		if (length(dv) < 3.0*0.3048)
		{
			double ApsidRot, h_peri, h_node;

			PMMLRBTI(sv_ephem);

			//Choose solution with the lowest LOI-1 DV
			if (PZLRBTI.sol[6].DVLOI1 > PZLRBTI.sol[7].DVLOI1)
			{
				loisol = 7;
			}
			else
			{
				loisol = 6;
			}

			h_peri = PZLRBTI.h_pc;
			h_node = PZLRBTI.sol[loisol].H_ND;
			ApsidRot = PZLRBTI.sol[loisol].f_ND_E - PZLRBTI.sol[loisol].f_ND_H;
			while (ApsidRot > 180.0)
			{
				ApsidRot -= 360.0;
			}
			while (ApsidRot < -180.0)
			{
				ApsidRot += 360.0;
			}

			//Maneuver execution criteria
			if (h_peri > 50.0 && h_peri < 70.0)
			{
				if (h_node > 50.0 && h_node < 75.0)
				{
					if (abs(ApsidRot) < 45.0)
					{
						scrubbed = true;
					}
				}
			}
		}

		if (scrubbed)
		{
			char buffer1[1000];

			sprintf(upMessage, "MCC-3 has been scrubbed");
			sprintf(upDesc, "CSM state vector and V66");

			AGCStateVectorUpdate(buffer1, RTCC_MPT_CSM, RTCC_MPT_CSM, sv_ephem, true);

			sprintf(uplinkdata, "%s", buffer1);
			if (upString != NULL) {
				// give to mcc
				strncpy(upString, uplinkdata, 1024 * 3);
			}
		}
		else
		{
			char buffer1[1000];
			char buffer2[1000];

			calcParams.LOI = PZMCCDIS.data[0].GET_LOI;

			engine = mcc->mcc_calcs.SPSRCSDecision(SPS_THRUST / WeightsTable.ConfigWeight, dv);
			PoweredFlightProcessor(sv_ephem, WeightsTable.CSMWeight, tig, engine, WeightsTable.LMAscWeight + WeightsTable.LMDscWeight, dv, false, P30TIG, dV_LVLH);

			manopt.TIG = P30TIG;
			manopt.dV_LVLH = dV_LVLH;
			manopt.enginetype = engine;
			manopt.HeadsUp = false;
			manopt.REFSMMAT = GetREFSMMATfromAGC(&mcc->cm->agc.vagc, true);
			manopt.RV_MCC = sv_ephem;
			manopt.WeightsTable = WeightsTable;

			AP11ManeuverPAD(manopt, *form);
			sprintf(form->purpose, "MCC-3");
			if (manopt.enginetype == RTCC_ENGINETYPE_CSMSPS) sprintf(form->remarks, "No ullage");

			AGCStateVectorUpdate(buffer1, RTCC_MPT_CSM, RTCC_MPT_CSM, sv_ephem, true);
			CMCExternalDeltaVUpdate(buffer2, P30TIG, dV_LVLH);

			sprintf(uplinkdata, "%s%s", buffer1, buffer2);
			if (upString != NULL) {
				// give to mcc
				strncpy(upString, uplinkdata, 1024 * 3);
				sprintf(upDesc, "CSM state vector and V66, target load");
			}
		}
	}
	break;
	case 16: //MCC-4
	{
		REFSMMATOpt refsopt;
		VECTOR3 dV_LVLH;
		EphemerisData sv;
		MATRIX3 REFSMMAT;
		double P30TIG, h_peri, h_node, ApsidRot;
		int engine, loisol;

		AP11MNV * form = (AP11MNV *)pad;

		sv = StateVectorCalcEphem(calcParams.src);

		bool loierr = PMMLRBTI(sv);

		if (loierr == false)
		{
			//Choose solution with the lowest LOI-1 DV
			if (PZLRBTI.sol[6].DVLOI1 > PZLRBTI.sol[7].DVLOI1)
			{
				loisol = 7;
			}
			else
			{
				loisol = 6;
			}

			h_peri = PZLRBTI.h_pc;
			h_node = PZLRBTI.sol[loisol].H_ND;
			ApsidRot = PZLRBTI.sol[loisol].f_ND_E - PZLRBTI.sol[loisol].f_ND_H;
			while (ApsidRot > 180.0)
			{
				ApsidRot -= 360.0;
			}
			while (ApsidRot < -180.0)
			{
				ApsidRot += 360.0;
			}

			//Maneuver execution criteria
			if (h_peri > 50.0 && h_peri < 70.0)
			{
				if (h_node > 50.0 && h_node < 75.0)
				{
					if (abs(ApsidRot) < 45.0)
					{
						scrubbed = true;
					}
				}
			}
		}

		//REFSMMAT calculation
		refsopt.LSAzi = LSAzi;
		refsopt.LSLat = BZLAND.lat[RTCC_LMPOS_BEST];
		refsopt.LSLng = BZLAND.lng[RTCC_LMPOS_BEST];
		refsopt.REFSMMATopt = 8;
		refsopt.REFSMMATTime = CZTDTGTU.GETTD;

		REFSMMAT = REFSMMATCalc(&refsopt);

		if (scrubbed)
		{
			char buffer1[1000];
			char buffer2[1000];

			sprintf(upMessage, "MCC-4 has been scrubbed");
			sprintf(upDesc, "CSM state vector and V66, Landing Site REFSMMAT");

			AGCStateVectorUpdate(buffer1, RTCC_MPT_CSM, RTCC_MPT_CSM, sv, true);
			AGCDesiredREFSMMATUpdate(buffer2, REFSMMAT);

			sprintf(uplinkdata, "%s%s", buffer1, buffer2);
			if (upString != NULL) {
				// give to mcc
				strncpy(upString, uplinkdata, 1024 * 3);
			}
		}
		else
		{
			AP11ManPADOpt manopt;
			char buffer1[1000];
			char buffer2[1000];
			char buffer3[1000];
			double tig;
			VECTOR3 dv;
			PLAWDTOutput WeightsTable;

			PZMCCPLN.MidcourseGET = calcParams.LOI - 5.0*3600.0;
			PZMCCPLN.Config = true;
			PZMCCPLN.Column = 1;
			PZMCCPLN.SFPBlockNum = 2;
			PZMCCPLN.Mode = 1;
			WeightsTable = GetWeightsTable(calcParams.src, true, true);

			TranslunarMidcourseCorrectionProcessor(sv, WeightsTable.CSMWeight, WeightsTable.LMAscWeight + WeightsTable.LMDscWeight);
			calcParams.LOI = PZMCCDIS.data[0].GET_LOI;

			tig = GETfromGMT(PZMCCXFR.sv_man_bef[0].GMT);
			dv = PZMCCXFR.V_man_after[0] - PZMCCXFR.sv_man_bef[0].V;

			engine = mcc->mcc_calcs.SPSRCSDecision(SPS_THRUST / WeightsTable.ConfigWeight, dv);
			PoweredFlightProcessor(sv, WeightsTable.CSMWeight, tig, engine, WeightsTable.LMAscWeight + WeightsTable.LMDscWeight, dv, false, P30TIG, dV_LVLH);

			manopt.TIG = P30TIG;
			manopt.dV_LVLH = dV_LVLH;
			manopt.enginetype = engine;
			manopt.HeadsUp = false;
			manopt.REFSMMAT = GetREFSMMATfromAGC(&mcc->cm->agc.vagc, true);
			manopt.RV_MCC = sv;
			manopt.WeightsTable = WeightsTable;

			AP11ManeuverPAD(manopt, *form);
			sprintf(form->purpose, "MCC-4");
			if (engine == RTCC_ENGINETYPE_CSMSPS) sprintf(form->remarks, "No ullage");

			AGCStateVectorUpdate(buffer1, RTCC_MPT_CSM, RTCC_MPT_CSM, sv, true);
			CMCExternalDeltaVUpdate(buffer2, P30TIG, dV_LVLH);
			AGCDesiredREFSMMATUpdate(buffer3, REFSMMAT);

			sprintf(uplinkdata, "%s%s%s", buffer1, buffer2, buffer3);
			if (upString != NULL) {
				// give to mcc
				strncpy(upString, uplinkdata, 1024 * 3);
				sprintf(upDesc, "CSM state vector and V66, target load, Landing Site REFSMMAT");
			}
		}
	}
	break;
	case 17: //PC+2 UPDATE
	{
		RTEMoonOpt entopt;
		EntryResults res;
		AP11ManPADOpt opt;
		SV sv;

		AP11MNV * form = (AP11MNV *)pad;

		sv = StateVectorCalc(calcParams.src);

		entopt.EntryLng = -165.0*RAD;
		entopt.returnspeed = 2;
		entopt.SMODE = 14;
		entopt.RV_MCC = sv;
		entopt.TIGguess = calcParams.LOI + 2.0*3600.0;
		entopt.vessel = calcParams.src;
		PZREAP.VRMAX = 37500.0;

		RTEMoonTargeting(&entopt, &res);//dV_LVLH, P30TIG, latitude, longitude, RET, RTGO, VIO, EntryAng);

		//Reset to default
		PZREAP.VRMAX = 36323.0;

		opt.TIG = res.P30TIG;
		opt.dV_LVLH = res.dV_LVLH;
		opt.enginetype = RTCC_ENGINETYPE_CSMSPS;
		opt.HeadsUp = false;
		opt.REFSMMAT = GetREFSMMATfromAGC(&mcc->cm->agc.vagc, true);
		opt.RV_MCC = ConvertSVtoEphemData(sv);
		opt.WeightsTable = GetWeightsTable(calcParams.src, true, false);

		AP11ManeuverPAD(opt, *form);

		if (!mcc->mcc_calcs.REFSMMATDecision(form->Att*RAD))
		{
			REFSMMATOpt refsopt;
			MATRIX3 REFSMMAT;

			refsopt.dV_LVLH = res.dV_LVLH;
			refsopt.REFSMMATTime = res.P30TIG;
			refsopt.REFSMMATopt = 0;
			refsopt.vessel = calcParams.src;

			REFSMMAT = REFSMMATCalc(&refsopt);

			opt.HeadsUp = true;
			opt.REFSMMAT = REFSMMAT;
			AP11ManeuverPAD(opt, *form);

			sprintf(form->remarks, "Requires realignment to preferred REFSMMAT");
		}
		sprintf(form->purpose, "PC+2");
		sprintf(form->remarks, "No ullage");
		form->lat = res.latitude*DEG;
		form->lng = res.longitude*DEG;
		form->RTGO = res.RTGO;
		form->VI0 = res.VIO / 0.3048;
		form->GET05G = res.GET05G;

		//Save parameters for further use
		SplashLatitude = res.latitude;
		SplashLongitude = res.longitude;
		calcParams.TEI = res.P30TIG;
		calcParams.EI = res.GET400K;
	}
	break;
	case 20:	// MISSION F PRELIMINARY LOI-1 MANEUVER
	case 21:	// MISSION F LOI-1 MANEUVER
	{
		AP11ManPADOpt manopt;
		double P30TIG, tig;
		VECTOR3 dV_LVLH, dv;
		int loisol;
		EphemerisData sv;
		PLAWDTOutput WeightsTable;

		AP11MNV * form = (AP11MNV *)pad;

		sv = StateVectorCalcEphem(calcParams.src); //State vector for uplink
		WeightsTable = GetWeightsTable(calcParams.src, true, true);

		PMMLRBTI(sv);

		//Choose solution with the lowest LOI-1 DV
		if (PZLRBTI.sol[6].DVLOI1 > PZLRBTI.sol[7].DVLOI1)
		{
			loisol = 7;
		}
		else
		{
			loisol = 6;
		}

		tig = GETfromGMT(PZLRBELM.sv_man_bef[loisol].GMT);
		dv = PZLRBELM.V_man_after[loisol] - PZLRBELM.sv_man_bef[loisol].V;
		PoweredFlightProcessor(sv, WeightsTable.CSMWeight, tig, RTCC_ENGINETYPE_CSMSPS, WeightsTable.LMAscWeight + WeightsTable.LMDscWeight, dv, false, P30TIG, dV_LVLH);

		manopt.TIG = P30TIG;
		manopt.dV_LVLH = dV_LVLH;
		manopt.enginetype = RTCC_ENGINETYPE_CSMSPS;
		manopt.HeadsUp = false;
		manopt.REFSMMAT = GetREFSMMATfromAGC(&mcc->cm->agc.vagc, true);
		manopt.RV_MCC = sv;
		manopt.WeightsTable = WeightsTable;

		AP11ManeuverPAD(manopt, *form);
		sprintf(form->purpose, "LOI-1");
		sprintf(form->remarks, "No ullage. LM weight is %.0f", form->LMWeight);

		TimeofIgnition = P30TIG;
		DeltaV_LVLH = dV_LVLH;

		if (fcn == 21)
		{
			char buffer1[1000];
			char buffer2[1000];

			AGCStateVectorUpdate(buffer1, RTCC_MPT_CSM, RTCC_MPT_CSM, sv, true);
			CMCExternalDeltaVUpdate(buffer2, P30TIG, dV_LVLH);

			sprintf(uplinkdata, "%s%s", buffer1, buffer2);
			if (upString != NULL) {
				// give to mcc
				strncpy(upString, uplinkdata, 1024 * 3);
				sprintf(upDesc, "CSM state vector and V66, target load");
			}
		}
	}
	break;
	case 22:	// MISSION F LOI-2 MANEUVER
	{
		AP11ManPADOpt manopt;
		double P30TIG;
		VECTOR3 dV_LVLH;
		SV sv;
		PLAWDTOutput WeightsTable;
		char buffer1[1000];
		char buffer2[1000];

		AP11MNV * form = (AP11MNV *)pad;

		sv = StateVectorCalc(calcParams.src); //State vector for uplink
		WeightsTable = GetWeightsTable(calcParams.src, true, true);

		med_k16.Mode = 2;
		med_k16.Sequence = 3;
		med_k16.GETTH1 = calcParams.LOI + 3.5*3600.0;
		med_k16.GETTH2 = med_k16.GETTH3 = med_k16.GETTH4 = med_k16.GETTH1;
		med_k16.DesiredHeight = 60.0*1852.0;

		LunarDescentPlanningProcessor(ConvertSVtoEphemData(sv), 0.0);
		PoweredFlightProcessor(sv, PZLDPDIS.GETIG[0], RTCC_ENGINETYPE_CSMSPS, WeightsTable.LMAscWeight + WeightsTable.LMDscWeight, PZLDPDIS.DVVector[0] * 0.3048, true, P30TIG, dV_LVLH);

		manopt.TIG = P30TIG;
		manopt.dV_LVLH = dV_LVLH;
		manopt.enginetype = RTCC_ENGINETYPE_CSMSPS;
		manopt.HeadsUp = false;
		manopt.REFSMMAT = GetREFSMMATfromAGC(&mcc->cm->agc.vagc, true);
		manopt.RV_MCC = ConvertSVtoEphemData(sv);
		manopt.WeightsTable = WeightsTable;

		AP11ManeuverPAD(manopt, *form);
		sprintf(form->purpose, "LOI-2");
		sprintf(form->remarks, "Two-jet ullage for 17 seconds");

		TimeofIgnition = P30TIG;
		DeltaV_LVLH = dV_LVLH;

		AGCStateVectorUpdate(buffer1, sv, true, true);
		CMCExternalDeltaVUpdate(buffer2, P30TIG, dV_LVLH);

		sprintf(uplinkdata, "%s%s", buffer1, buffer2);
		if (upString != NULL) {
			// give to mcc
			strncpy(upString, uplinkdata, 1024 * 3);
			sprintf(upDesc, "CSM state vector and V66, target load");
		}
	}
	break;
	case 30: //TEI-1 UPDATE (PRE LOI-1)
	case 31: //TEI-4 UPDATE (PRE LOI-1)
	case 32: //TEI-5 UPDATE (PRE LOI-2)
	case 33: //TEI-10 UPDATE
	case 34: //TEI-22 UPDATE
	case 35: //TEI-23 UPDATE
	case 36: //TEI-24 UPDATE
	case 37: //TEI-25 UPDATE
	case 38: //TEI-26 UPDATE
	case 39: //TEI-27 UPDATE
	case 130: //TEI-29 UPDATE
	case 131: //TEI-30 UPDATE
	case 132: //PRELIMINARY TEI-31 UPDATE
	case 133: //FINAL TEI-31 UPDATE
	case 134: //TEI-32 UPDATE
	{
		RTEMoonOpt entopt;
		EntryResults res;
		AP11ManPADOpt opt;
		SV sv0, sv1, sv2;
		char manname[8];

		AP11MNV * form = (AP11MNV *)pad;

		sv0 = StateVectorCalc(calcParams.src); //State vector for uplink

		if (fcn == 30 || fcn == 31 || fcn == 32)
		{
			sv1 = ExecuteManeuver(sv0, TimeofIgnition, DeltaV_LVLH, GetDockedVesselMass(calcParams.src), RTCC_ENGINETYPE_CSMSPS);
		}
		else
		{
			sv1 = sv0;
		}

		if (fcn == 30)
		{
			sprintf(manname, "TEI-1");
			sv2 = coast(sv1, 0.5*2.0*3600.0);
		}
		else if (fcn == 31)
		{
			sprintf(manname, "TEI-4");
			sv2 = coast(sv1, 3.5*2.0*3600.0);
		}
		else if (fcn == 32)
		{
			sprintf(manname, "TEI-5");
			sv2 = coast(sv1, 2.5*2.0*3600.0);
		}
		else if (fcn == 33)
		{
			sprintf(manname, "TEI-10");
			sv2 = coast(sv1, 5.5*2.0*3600.0);
		}
		else if (fcn == 34)
		{
			sprintf(manname, "TEI-22");
			sv2 = coast(sv1, 5.5*2.0*3600.0);
		}
		else if (fcn == 35)
		{
			sprintf(manname, "TEI-23");
			sv2 = coast(sv1, 0.5*2.0*3600.0);
		}
		else if (fcn == 36)
		{
			sprintf(manname, "TEI-24");
			sv2 = coast(sv1, 0.5*2.0*3600.0);
		}
		else if (fcn == 37)
		{
			sprintf(manname, "TEI-25");
			sv2 = coast(sv1, 0.5*2.0*3600.0);
		}
		else if (fcn == 38)
		{
			sprintf(manname, "TEI-26");
			sv2 = coast(sv1, 0.5*2.0*3600.0);
		}
		else if (fcn == 39)
		{
			sprintf(manname, "TEI-27");
			sv2 = coast(sv1, 0.5*2.0*3600.0);
		}
		else if (fcn == 130)
		{
			sprintf(manname, "TEI-29");
			sv2 = coast(sv1, 2.5*2.0*3600.0);
		}
		else if (fcn == 131)
		{
			sprintf(manname, "TEI-30");
			sv2 = coast(sv1, 1.0*2.0*3600.0);
		}
		else if (fcn == 132)
		{
			sprintf(manname, "TEI-31");
			sv2 = coast(sv1, 1.0*2.0*3600.0);
		}
		else if (fcn == 133)
		{
			sprintf(manname, "TEI-31");
			sv2 = sv1;
		}
		else if (fcn == 134)
		{
			sprintf(manname, "TEI-32");
			sv2 = coast(sv1, 1.5*2.0*3600.0);
		}

		entopt.EntryLng = -165.0*RAD;
		entopt.returnspeed = 1;
		entopt.RV_MCC = sv2;
		entopt.vessel = calcParams.src;
		//It gets close to the nominal 36232 ft/s constraint, so relax it a little bit
		PZREAP.VRMAX = 36500.0;

		RTEMoonTargeting(&entopt, &res);

		//Reset to default
		PZREAP.VRMAX = 36323.0;

		opt.TIG = res.P30TIG;
		opt.dV_LVLH = res.dV_LVLH;
		opt.enginetype = RTCC_ENGINETYPE_CSMSPS;
		opt.HeadsUp = false;
		opt.REFSMMAT = GetREFSMMATfromAGC(&mcc->cm->agc.vagc, true);
		opt.sxtstardtime = -15.0*60.0; //15 minutes before TIG
		opt.RV_MCC = ConvertSVtoEphemData(sv1);
		opt.WeightsTable.CC[RTCC_CONFIG_C] = true;
		opt.WeightsTable.ConfigWeight = opt.WeightsTable.CSMWeight = sv1.mass;

		AP11ManeuverPAD(opt, *form);

		sprintf(form->purpose, manname);
		sprintf(form->remarks, "Two-jet ullage for 14 seconds");
		form->lat = res.latitude*DEG;
		form->lng = res.longitude*DEG;
		form->RTGO = res.RTGO;
		form->VI0 = res.VIO / 0.3048;
		form->GET05G = res.GET05G;

		if (fcn != 134)
		{
			//Save parameters for further use
			SplashLatitude = res.latitude;
			SplashLongitude = res.longitude;
			calcParams.TEI = res.P30TIG;
			calcParams.EI = res.GET400K;
		}

		if (fcn == 133)
		{
			char buffer1[1000];
			char buffer2[1000];

			TimeofIgnition = res.P30TIG;
			DeltaV_LVLH = res.dV_LVLH;

			AGCStateVectorUpdate(buffer1, sv0, true, true);
			CMCExternalDeltaVUpdate(buffer2, TimeofIgnition, DeltaV_LVLH);

			sprintf(uplinkdata, "%s%s", buffer1, buffer2);
			if (upString != NULL) {
				// give to mcc
				strncpy(upString, uplinkdata, 1024 * 3);
				sprintf(upDesc, "State vector and V66, target load");
			}
		}
	}
	break;
	case 40: //REV 1 MAP UPDATE
	{
		EphemerisData sv0, sv1, sv2;
		PLAWDTOutput WeightsTable, WeightsTable2;
		AP10MAPUPDATE upd_hyper, upd_ellip;

		AP10MAPUPDATE * form = (AP10MAPUPDATE *)pad;

		sv0 = StateVectorCalcEphem(calcParams.src);
		WeightsTable = GetWeightsTable(calcParams.src, true, true);

		LunarOrbitMapUpdate(sv0, upd_hyper);

		ExecuteManeuver(sv0, WeightsTable, TimeofIgnition, DeltaV_LVLH, RTCC_ENGINETYPE_CSMSPS, sv1, WeightsTable2);
		sv2 = coast(sv1, -30.0*60.0);
		LunarOrbitMapUpdate(sv2, upd_ellip);

		form->type = 6;
		form->Rev = 1;
		form->AOSGET = upd_ellip.AOSGET;
		form->LOSGET = upd_hyper.LOSGET;
		form->PMGET = upd_hyper.PMGET;
	}
	break;
	case 41: //REV 2 MAP UPDATE
	case 43: //REV 4 MAP UPDATE
	case 44: //REV 11 MAP UPDATE
	case 45: //REV 22 MAP UPDATE
	case 46: //REV 23 MAP UPDATE
	case 47: //REV 24 MAP UPDATE
	case 48: //REV 25 MAP UPDATE
	case 49: //REV 26 MAP UPDATE
	case 140: //REV 27 MAP UPDATE
	case 141: //REV 29 MAP UPDATE
	case 142: //REV 30 MAP UPDATE
	case 143: //REV 31 MAP UPDATE
	{
		EphemerisData sv0, sv1;

		AP10MAPUPDATE * form = (AP10MAPUPDATE *)pad;

		sv0 = StateVectorCalcEphem(calcParams.src);

		if (fcn == 45)
		{
			sv1 = coast(sv0, 2.0*4.0*3600.0);
		}
		else if (fcn == 141)
		{
			sv1 = coast(sv0, 1.0*2.0*3600.0);
		}
		else
		{
			sv1 = sv0;
		}

		LunarOrbitMapUpdate(sv1, *form);
		form->type = 6;

		if (fcn == 41)
		{
			form->Rev = 2;
		}
		else if (fcn == 43)
		{
			form->Rev = 4;
		}
		else if (fcn == 44)
		{
			form->Rev = 11;
		}
		else if (fcn == 45 || fcn == 46 || fcn == 47 || fcn == 48 || fcn == 49)
		{
			form->Rev = fcn - 23;
		}
		else if (fcn == 140)
		{
			form->Rev = 27;
		}
		else if (fcn == 141)
		{
			form->Rev = 29;
		}
		else if (fcn == 142)
		{
			form->Rev = 30;
		}
		else if (fcn == 143)
		{
			form->Rev = 31;
		}
	}
	break;
	case 42: //REV 3 MAP UPDATE
	{
		EphemerisData sv0, sv1;
		PLAWDTOutput WeightsTable, WeightsTable2;
		AP10MAPUPDATE upd_preloi, upd_postloi;

		AP10MAPUPDATE * form = (AP10MAPUPDATE *)pad;

		sv0 = StateVectorCalcEphem(calcParams.src);
		WeightsTable = GetWeightsTable(calcParams.src, true, true);

		LunarOrbitMapUpdate(sv0, upd_preloi);

		ExecuteManeuver(sv0, WeightsTable, TimeofIgnition, DeltaV_LVLH, RTCC_ENGINETYPE_CSMSPS, sv1, WeightsTable2);
		LunarOrbitMapUpdate(sv1, upd_postloi);

		form->type = 6;
		form->Rev = 3;
		form->AOSGET = upd_postloi.AOSGET;
		form->LOSGET = upd_preloi.LOSGET;
		form->PMGET = upd_preloi.PMGET;
	}
	break;
	case 144: //TEI MAP UPDATE
	{
		EphemerisData sv0, sv1;
		PLAWDTOutput WeightsTable, WeightsTable2;
		AP10MAPUPDATE upd_pretei, upd_posttei;

		AP10MAPUPDATE * form = (AP10MAPUPDATE *)pad;

		sv0 = StateVectorCalcEphem(calcParams.src);
		WeightsTable = GetWeightsTable(calcParams.src, true, false);

		LunarOrbitMapUpdate(sv0, upd_pretei);

		ExecuteManeuver(sv0, WeightsTable, TimeofIgnition, DeltaV_LVLH, RTCC_ENGINETYPE_CSMSPS, sv1, WeightsTable2);

		LunarOrbitMapUpdate(sv1, upd_posttei);

		form->type = 7;
		form->Rev = 32;
		form->AOSGET = upd_pretei.AOSGET;
		form->AOSGET2 = upd_posttei.AOSGET;
		form->LOSGET = upd_pretei.LOSGET;
	}
	break;
	case 50: //REV 4 LANDMARK TRACKING PAD F-1
	case 51: //REV 4 LANDMARK TRACKING PAD B-1
	case 52: //REV 11 LANDMARK TRACKING PAD LLS-2
	case 53: //REV 24 LANDMARK TRACKING PADs
	case 54: //REV 25 LANDMARK TRACKING PADs
	case 55: //REV 26 LANDMARK TRACKING PADs
	case 56: //REV 27 LANDMARK TRACKING PADs
	case 57: //REV 30 LANDMARK TRACKING PADs
	{
		LMARKTRKPADOpt opt;
		EphemerisData sv0;

		AP11LMARKTRKPAD * form = (AP11LMARKTRKPAD *)pad;

		sv0 = StateVectorCalcEphem(calcParams.src);

		opt.sv0 = sv0;

		if (fcn == 50)
		{
			sprintf(form->LmkID[0], "F-1");
			opt.alt[0] = 0;
			opt.lat[0] = 1.6*RAD;
			opt.LmkTime[0] = OrbMech::HHMMSSToSS(82, 27, 0);
			opt.lng[0] = 86.88*RAD;
			opt.entries = 1;
		}
		else if (fcn == 51)
		{
			sprintf(form->LmkID[0], "B-1");
			opt.alt[0] = -1.54*1852.0;
			opt.lat[0] = 2.522*RAD;
			opt.LmkTime[0] = OrbMech::HHMMSSToSS(82, 45, 0);
			opt.lng[0] = 35.036*RAD;
			opt.entries = 1;
		}
		else if (fcn == 52)
		{
			sprintf(form->LmkID[0], "130");
			opt.alt[0] = -1.73*1852.0;
			opt.lat[0] = 1.266*RAD;
			opt.LmkTime[0] = OrbMech::HHMMSSToSS(96, 35, 0);
			opt.lng[0] = 23.678*RAD;
			opt.entries = 1;
		}
		else if (fcn == 53 || fcn == 54 || fcn == 55 || fcn == 56)
		{
			if (fcn == 53)
			{
				opt.LmkTime[0] = OrbMech::HHMMSSToSS(121, 26, 0);
				opt.LmkTime[1] = OrbMech::HHMMSSToSS(121, 41, 0);
				opt.LmkTime[2] = OrbMech::HHMMSSToSS(121, 54, 0);
				opt.LmkTime[3] = OrbMech::HHMMSSToSS(122, 15, 0);
			}
			else if (fcn == 54)
			{
				opt.LmkTime[0] = OrbMech::HHMMSSToSS(123, 24, 0);
				opt.LmkTime[1] = OrbMech::HHMMSSToSS(123, 39, 0);
				opt.LmkTime[2] = OrbMech::HHMMSSToSS(123, 52, 0);
				opt.LmkTime[3] = OrbMech::HHMMSSToSS(124, 13, 0);
			}
			else if (fcn == 55)
			{
				opt.LmkTime[0] = OrbMech::HHMMSSToSS(125, 22, 0);
				opt.LmkTime[1] = OrbMech::HHMMSSToSS(125, 37, 0);
				opt.LmkTime[2] = OrbMech::HHMMSSToSS(125, 50, 0);
				opt.LmkTime[3] = OrbMech::HHMMSSToSS(126, 11, 0);
			}
			else if (fcn == 56)
			{
				opt.LmkTime[0] = OrbMech::HHMMSSToSS(127, 20, 0);
				opt.LmkTime[1] = OrbMech::HHMMSSToSS(127, 35, 0);
				opt.LmkTime[2] = OrbMech::HHMMSSToSS(127, 48, 0);
				opt.LmkTime[3] = OrbMech::HHMMSSToSS(128, 9, 0);
			}

			sprintf(form->LmkID[0], "CP-1");
			opt.alt[0] = 0.0;
			opt.lat[0] = 0.875*RAD;
			opt.lng[0] = 170.146*RAD;

			sprintf(form->LmkID[1], "CP-2");
			opt.alt[1] = 0.0;
			opt.lat[1] = 1.0*RAD;
			opt.lng[1] = 127.4*RAD;

			sprintf(form->LmkID[2], "F-1");
			opt.alt[2] = 0.0;
			opt.lat[2] = 1.6*RAD;
			opt.lng[2] = 86.88*RAD;

			sprintf(form->LmkID[3], "130");
			opt.alt[3] = -1.73*1852.0;
			opt.lat[3] = 1.266*RAD;
			opt.lng[3] = 23.678*RAD;

			opt.entries = 4;
		}
		else if (fcn == 57)
		{
			sprintf(form->LmkID[0], "B-1");
			opt.alt[0] = -1.54*1852.0;
			opt.lat[0] = 2.522*RAD;
			opt.LmkTime[0] = OrbMech::HHMMSSToSS(134, 0, 0);
			opt.lng[0] = 35.036*RAD;

			sprintf(form->LmkID[1], "150");
			opt.alt[1] = -1.05*1852.0;
			opt.lat[1] = 0.283*RAD;
			opt.LmkTime[1] = OrbMech::HHMMSSToSS(134, 12, 0);
			opt.lng[1] = -1.428*RAD;

			opt.entries = 2;
		}

		LandmarkTrackingPAD(opt, *form);
	}
	break;
	case 60: //STATE VECTOR and LLS 2 REFSMMAT UPLINK
	{
		MATRIX3 REFSMMAT;
		SV sv;
		REFSMMATOpt opt;
		char buffer1[1000];
		char buffer2[1000];

		sv = StateVectorCalc(calcParams.src); //State vector for uplink

		//MED K17
		GZGENCSN.LDPPAzimuth = 0.0;
		GZGENCSN.LDPPHeightofPDI = 50000.0*0.3048;
		GZGENCSN.LDPPPoweredDescentSimFlag = false;
		GZGENCSN.LDPPDwellOrbits = 0;
		GZGENCSN.LDPPDescentFlightArc = GZGENCSN.LDPPLandingSiteOffset = 14.51*RAD;
		//MED K16
		med_k16.Mode = 4;
		med_k16.Sequence = 1;
		med_k16.GETTH1 = med_k16.GETTH2 = med_k16.GETTH3 = med_k16.GETTH4 = OrbMech::HHMMSSToSS(99, 0, 0);

		LunarDescentPlanningProcessor(ConvertSVtoEphemData(sv), 0.0);

		calcParams.DOI = GETfromGMT(PZLDPELM.sv_man_bef[0].GMT);
		CZTDTGTU.GETTD = PZLDPDIS.PD_GETTD;

		opt.LSLat = BZLAND.lat[RTCC_LMPOS_BEST];
		opt.LSLng = BZLAND.lng[RTCC_LMPOS_BEST];
		opt.REFSMMATopt = 5;
		opt.REFSMMATTime = CZTDTGTU.GETTD;
		opt.vessel = calcParams.src;

		REFSMMAT = REFSMMATCalc(&opt);

		AGCStateVectorUpdate(buffer1, sv, true, true);
		AGCDesiredREFSMMATUpdate(buffer2, REFSMMAT);

		sprintf(uplinkdata, "%s%s", buffer1, buffer2);
		if (upString != NULL) {
			// give to mcc
			strncpy(upString, uplinkdata, 1024 * 3);
			sprintf(upDesc, "CSM state vector and V66, LLS2 REFSMMAT");
		}
	}
	break;
	case 61: //CSM DAP DATA
	{
		AP10DAPDATA * form = (AP10DAPDATA *)pad;

		CSMDAPUpdate(calcParams.src, *form, false);
	}
	break;
	case 62: //LM DAP DATA
	{
		AP10DAPDATA * form = (AP10DAPDATA *)pad;

		LMDAPUpdate(calcParams.tgt, *form, false);
	}
	break;
	case 63: //GYRO TORQUING ANGLES
	{
		TORQANG * form = (TORQANG *)pad;
		LEM *lem = (LEM *)calcParams.tgt;

		VECTOR3 lmn20, csmn20, V42angles;

		csmn20.x = calcParams.src->imu.Gimbal.X;
		csmn20.y = calcParams.src->imu.Gimbal.Y;
		csmn20.z = calcParams.src->imu.Gimbal.Z;

		lmn20.x = lem->imu.Gimbal.X;
		lmn20.y = lem->imu.Gimbal.Y;
		lmn20.z = lem->imu.Gimbal.Z;

		V42angles = OrbMech::LMDockedFineAlignment(lmn20, csmn20);

		form->V42Angles.x = V42angles.x*DEG;
		form->V42Angles.y = V42angles.y*DEG;
		form->V42Angles.z = V42angles.z*DEG;
	}
	break;
	case 64: //LGC ACTIVATION UPDATE
	{
		SV sv;
		REFSMMATOpt opt;
		MATRIX3 REFSMMAT;
		double TEPHEM0, tephem, t_AGC, t_actual, deltaT;
		LEM *lem;
		char clockupdate[128];
		char buffer1[1000];
		char buffer2[1000];
		char buffer3[1000];

		sv = StateVectorCalc(calcParams.src); //State vector for uplink
		lem = (LEM *)calcParams.tgt;
		TEPHEM0 = 40038.;

		tephem = GetTEPHEMFromAGC(&lem->agc.vagc, false);
		t_AGC = GetClockTimeFromAGC(&lem->agc.vagc) / 100.0;

		tephem = (tephem / 8640000.) + TEPHEM0;
		t_actual = (oapiGetSimMJD() - tephem) * 86400.;
		deltaT = t_actual - t_AGC;

		IncrementAGCTime(clockupdate, RTCC_MPT_LM, deltaT);

		opt.LSLat = BZLAND.lat[RTCC_LMPOS_BEST];
		opt.LSLng = BZLAND.lng[RTCC_LMPOS_BEST];
		opt.REFSMMATopt = 5;
		opt.REFSMMATTime = CZTDTGTU.GETTD;
		opt.vessel = calcParams.src;

		REFSMMAT = REFSMMATCalc(&opt);

		AGCStateVectorUpdate(buffer1, sv, true);
		AGCStateVectorUpdate(buffer2, sv, false);
		AGCREFSMMATUpdate(buffer3, REFSMMAT, false);

		sprintf(uplinkdata, "%s%s%s%s", clockupdate, buffer1, buffer2, buffer3);
		if (upString != NULL) {
			// give to mcc
			strncpy(upString, uplinkdata, 1024 * 3);
			sprintf(upDesc, "Clock update, state vectors, LS REFSMMAT");
		}
	}
	break;
	case 65: //AGS ACTIVATION UPDATE
	{
		GENERICPAD *form = (GENERICPAD*)pad;

		double KFactor, ss;
		int hh, mm;

		LEM *l = (LEM*)calcParams.tgt;
		bool res_k = CalculateAGSKFactor(&l->agc.vagc, &l->aea.vags, KFactor);

		//Sanity check on K-Factor value
		if (!res_k || abs(KFactor - 90.0*3600.0) > 2.0 * 60.0)
		{
			KFactor = 90.0*3600.0; //Default to 90h if no reasonable K-Factor was determined
		}

		SystemParameters.MCGZSS = SystemParameters.MCGZSL + KFactor / 3600.0;

		OrbMech::SStoHHMMSS(GETfromGMT(GetAGSClockZero()), hh, mm, ss, 0.01);

		sprintf(form->paddata, "K-Factor: %03d:%02d:%05.2f GET", hh, mm, ss);
	}
	break;
	case 70: //CSM SEPARATION BURN
	{
		AP11ManPADOpt opt;
		EphemerisData sv;
		VECTOR3 dV_LVLH;
		double t_P, t_Undock;
		char buffer1[1000];
		char buffer2[1000];
		char GETBuffer[128];

		AP11MNV * form = (AP11MNV *)pad;

		sv = StateVectorCalcEphem(calcParams.src); //State vector for uplink

		//Separation burn half an orbit before DOI
		t_P = OrbMech::period(sv.R, sv.V, OrbMech::mu_Moon);
		calcParams.SEP = floor(calcParams.DOI - t_P / 2.0);
		//Undocking 25 minutes (rounded down to the previous minute) before sep
		t_Undock = floor((calcParams.SEP - 25.0*60.0) / 60.0)*60.0;

		dV_LVLH = _V(0, 0, -2.5)*0.3048;

		opt.TIG = calcParams.SEP;
		opt.dV_LVLH = dV_LVLH;
		opt.enginetype = RTCC_ENGINETYPE_CSMRCSPLUS4;
		opt.HeadsUp = true;
		opt.REFSMMAT = GetREFSMMATfromAGC(&mcc->cm->agc.vagc, true);
		opt.RV_MCC = sv;
		opt.WeightsTable = GetWeightsTable(calcParams.src, true, false);

		AP11ManeuverPAD(opt, *form);
		sprintf(form->purpose, "Separation");
		form->type = 2;
		OrbMech::format_time_HHMMSS(GETBuffer, t_Undock);
		sprintf(form->remarks, "Undocking at %s GET", GETBuffer);

		AGCStateVectorUpdate(buffer1, 1, RTCC_MPT_CSM, sv);
		AGCStateVectorUpdate(buffer2, 1, RTCC_MPT_LM, sv);

		sprintf(uplinkdata, "%s%s", buffer1, buffer2);
		if (upString != NULL) {
			// give to mcc
			strncpy(upString, uplinkdata, 1024 * 3);
			sprintf(upDesc, "State vectors");
		}
	}
	break;
	case 71: //DESCENT ORBIT INSERTION
	{
		AP11LMManPADOpt opt;

		VECTOR3 DV;
		double GETbase, t_DOI_imp, t_TPI_guess;
		SV sv_CSM, sv, sv_DOI;
		PLAWDTOutput WeightsTable;
		char GETbuffer[64];
		char TLANDbuffer[64];
		char buffer1[1000];
		char buffer2[1000];

		AP11LMMNV * form = (AP11LMMNV *)pad;

		sv_CSM = StateVectorCalc(calcParams.src);
		sv = StateVectorCalc(calcParams.tgt);
		GETbase = CalcGETBase();
		WeightsTable = GetWeightsTable(calcParams.tgt, false, false);

		//MED K17
		GZGENCSN.LDPPAzimuth = 0.0;
		GZGENCSN.LDPPHeightofPDI = 50000.0*0.3048;
		GZGENCSN.LDPPPoweredDescentSimFlag = false;
		GZGENCSN.LDPPDwellOrbits = 0;
		GZGENCSN.LDPPDescentFlightArc = GZGENCSN.LDPPLandingSiteOffset = 14.51*RAD;
		//MED K16
		med_k16.Mode = 4;
		med_k16.Sequence = 1;
		med_k16.GETTH1 = med_k16.GETTH2 = med_k16.GETTH3 = med_k16.GETTH4 = OrbMech::HHMMSSToSS(99, 0, 0);

		LunarDescentPlanningProcessor(ConvertSVtoEphemData(sv), 0.0);

		calcParams.DOI = t_DOI_imp = GETfromGMT(PZLDPELM.sv_man_bef[0].GMT);
		CZTDTGTU.GETTD = PZLDPDIS.PD_GETTD;
		DV = PZLDPELM.V_man_after[0] - PZLDPELM.sv_man_bef[0].V;

		PoweredFlightProcessor(sv, t_DOI_imp, RTCC_ENGINETYPE_LMDPS, 0.0, DV, false, TimeofIgnition, DeltaV_LVLH);

		opt.TIG = TimeofIgnition;
		opt.dV_LVLH = DeltaV_LVLH;
		opt.enginetype = RTCC_ENGINETYPE_LMDPS;
		opt.HeadsUp = true;
		opt.REFSMMAT = GetREFSMMATfromAGC(&mcc->lm->agc.vagc, false);
		opt.RV_MCC = ConvertSVtoEphemData(sv);
		opt.WeightsTable = WeightsTable;

		AP11LMManeuverPAD(opt, *form);
		sprintf(form->purpose, "DOI");

		//Rendezvous Plan
		double MJD_Phasing;

		sv_DOI = ExecuteManeuver(sv, TimeofIgnition, DeltaV_LVLH, 0.0, RTCC_ENGINETYPE_LMDPS);
		MJD_Phasing = OrbMech::P29TimeOfLongitude(SystemParameters.MAT_J2000_BRCS, sv_DOI.R, sv_DOI.V, sv_DOI.MJD, sv_DOI.gravref, -12.5*RAD);
		calcParams.Phasing = (MJD_Phasing - GETbase)*24.0*3600.0;

		t_TPI_guess = OrbMech::HHMMSSToSS(105, 9, 0);
		calcParams.TPI = mcc->mcc_calcs.FindOrbitalMidnight(sv_CSM, t_TPI_guess);

		mcc->mcc_calcs.FMissionRendezvousPlan(calcParams.tgt, calcParams.src, sv_DOI, calcParams.Phasing, calcParams.TPI, calcParams.Insertion, calcParams.CSI);

		OrbMech::format_time_HHMMSS(GETbuffer, calcParams.CSI);
		sprintf(form->remarks, "CSI time: %s, ", GETbuffer);
		OrbMech::format_time_HHMMSS(GETbuffer, calcParams.TPI);
		sprintf(form->remarks, "%sTPI time: %s, N equal to 1", form->remarks, GETbuffer);

		AGCStateVectorUpdate(buffer1, sv, false);
		LGCExternalDeltaVUpdate(buffer2, TimeofIgnition, DeltaV_LVLH);
		TLANDUpdate(TLANDbuffer, CZTDTGTU.GETTD);

		sprintf(uplinkdata, "%s%s%s", buffer1, buffer2, TLANDbuffer);
		if (upString != NULL) {
			// give to mcc
			strncpy(upString, uplinkdata, 1024 * 3);
			sprintf(upDesc, "LM state vector, DOI target load");
		}
	}
	break;
	case 72: //PRELIMINARY PHASING MANEUVER
		preliminary = true;
	case 73: //PHASING MANEUVER
	{
		AP11LMManPADOpt opt;
		LambertMan lamopt;
		TwoImpulseResuls res;
		EphemerisData sv_CSM, sv_LM, sv_DOI;
		SV sv_DOI2;
		PLAWDTOutput WeightsTable_CSM, WeightsTable_LM, WeightsTable_LM2;
		VECTOR3 dV_LVLH;
		double GETbase, MJD_LS, t_LS, P30TIG, MJD_100E, t_100E;
		char GETbuffer[64];
		char GETbuffer2[64];

		AP11LMMNV * form = (AP11LMMNV *)pad;
		GETbase = CalcGETBase();

		sv_CSM = StateVectorCalcEphem(calcParams.src);
		WeightsTable_CSM = GetWeightsTable(calcParams.src, true, false);

		sv_LM = StateVectorCalcEphem(calcParams.tgt);
		WeightsTable_LM = GetWeightsTable(calcParams.tgt, false, false);

		if (preliminary)
		{
			ExecuteManeuver(sv_LM, WeightsTable_LM, TimeofIgnition, DeltaV_LVLH, RTCC_ENGINETYPE_LMDPS, sv_DOI, WeightsTable_LM2);
		}
		else
		{
			sv_DOI = sv_LM;
			WeightsTable_LM2 = WeightsTable_LM;
		}
		sv_DOI2 = ConvertEphemDatatoSV(sv_DOI, WeightsTable_LM2.ConfigWeight);

		lamopt.axis = RTCC_LAMBERT_MULTIAXIS;
		lamopt.mode = 0;
		lamopt.N = 0;
		lamopt.Offset = _V(-270.0*1852.0, 0.0, 60.0*1852.0 - 60000.0*0.3048);
		lamopt.Perturbation = RTCC_LAMBERT_PERTURBED;
		lamopt.sv_A = sv_DOI2;
		lamopt.sv_P = ConvertEphemDatatoSV(sv_CSM, WeightsTable_CSM.ConfigWeight);
		lamopt.T1 = calcParams.Phasing;
		lamopt.T2 = calcParams.Insertion;

		LambertTargeting(&lamopt, res);
		PoweredFlightProcessor(sv_DOI2, lamopt.T1, RTCC_ENGINETYPE_LMDPS, 0.0, res.dV, false, P30TIG, dV_LVLH);

		opt.TIG = P30TIG;
		opt.dV_LVLH = dV_LVLH;
		opt.enginetype = RTCC_ENGINETYPE_LMDPS;
		opt.HeadsUp = false;
		opt.REFSMMAT = GetREFSMMATfromAGC(&mcc->lm->agc.vagc, false);
		opt.RV_MCC = sv_DOI;
		opt.WeightsTable = WeightsTable_LM2;

		AP11LMManeuverPAD(opt, *form);
		sprintf(form->purpose, "Phasing");

		if (preliminary)
		{
			MJD_LS = OrbMech::P29TimeOfLongitude(SystemParameters.MAT_J2000_BRCS, sv_DOI.R, sv_DOI.V, sv_DOI2.MJD, sv_DOI2.gravref, BZLAND.lng[RTCC_LMPOS_BEST]);
			t_LS = (MJD_LS - GETbase)*24.0*3600.0;
			MJD_100E = OrbMech::P29TimeOfLongitude(SystemParameters.MAT_J2000_BRCS, sv_DOI.R, sv_DOI.V, sv_DOI2.MJD, sv_DOI2.gravref, 100.0*RAD);
			t_100E = (MJD_100E - GETbase)*24.0*3600.0;

			OrbMech::format_time_MMSS(GETbuffer, P30TIG - t_100E);
			OrbMech::format_time_MMSS(GETbuffer2, P30TIG - t_LS);
			sprintf(form->remarks, "100-degree east time is %s. Site 2 time is %s", GETbuffer, GETbuffer2);
		}
	}
	break;
	case 74: //PDI ABORT MANEUVER
	{
		AP11LMManPADOpt opt;
		DKIOpt dkiopt;
		EphemerisData sv_LM, sv_CSM, sv_DOI, sv_Phasing;
		PLAWDTOutput WeightsTable_LM, WeightsTable_LM2;
		VECTOR3 dV_LVLH;
		double GETbase, dt_peri, t_Abort, t_TPI_guess, t_TPI_Abort, P30TIG;
		char GETbuffer[64], GETbuffer2[64];

		AP11LMMNV * form = (AP11LMMNV *)pad;
		GETbase = CalcGETBase();

		sv_CSM = StateVectorCalcEphem(calcParams.src);
		sv_LM = StateVectorCalcEphem(calcParams.tgt);
		WeightsTable_LM = GetWeightsTable(calcParams.tgt, false, false);

		ExecuteManeuver(sv_LM, WeightsTable_LM, TimeofIgnition, DeltaV_LVLH, RTCC_ENGINETYPE_LMDPS, sv_DOI, WeightsTable_LM2);

		dt_peri = OrbMech::timetoperi(sv_DOI.R, sv_DOI.V, OrbMech::mu_Moon);
		t_Abort = sv_DOI.GMT + dt_peri;

		t_TPI_guess = OrbMech::HHMMSSToSS(103, 9, 0);
		t_TPI_Abort = mcc->mcc_calcs.FindOrbitalMidnight(ConvertEphemDatatoSV(sv_CSM, 10000.0), t_TPI_guess);

		dkiopt.DHSR = 15.0*1852.0;
		dkiopt.Elev = 26.6*RAD;
		dkiopt.IPUTNA = 1;
		dkiopt.PUTNA = 0.5;
		dkiopt.PUTTNA = t_Abort;
		dkiopt.NC1 = 0.5;
		dkiopt.NH = 1.0;
		dkiopt.NSR = 1.5;
		dkiopt.K46 = 1;
		dkiopt.WT = 130.0*RAD;
		dkiopt.MI = 2.0;
		dkiopt.MV = 2;
		dkiopt.sv_CSM = sv_CSM;
		dkiopt.sv_LM = sv_DOI;
		dkiopt.TTPI = GMTfromGET(t_TPI_Abort);

		DockingInitiationProcessor(dkiopt);

		//Convert to finite burn
		PMMMPTInput in;

		in.CONFIG = RTCC_CONFIG_A + RTCC_CONFIG_D;
		in.VC = RTCC_MANVEHICLE_LM;
		in.CSMWeight = 0.0;
		in.LMWeight = WeightsTable_LM2.ConfigWeight;
		in.VehicleArea = 0.0;
		in.IterationFlag = false;
		in.IgnitionTimeOption = false;
		in.Thruster = RTCC_ENGINETYPE_LMDPS;

		in.sv_before = PZDKIELM.Block[0].SV_before[0];
		in.V_aft = PZDKIELM.Block[0].V_after[0];
		in.DETU = 8.0;
		in.UT = true;
		in.DT_10PCT = -15.0; //Minus to bypass throttle up test
		in.DPSScaleFactor = 0.925;

		double GMT_TIG;
		PoweredFlightProcessor(in, GMT_TIG, dV_LVLH);
		P30TIG = GETfromGMT(GMT_TIG);

		opt.TIG = P30TIG;
		opt.dV_LVLH = dV_LVLH;
		opt.enginetype = RTCC_ENGINETYPE_LMDPS;
		opt.HeadsUp = false;
		opt.REFSMMAT = GetREFSMMATfromAGC(&mcc->lm->agc.vagc, false);
		opt.RV_MCC = sv_DOI;
		opt.WeightsTable = WeightsTable_LM2;

		AP11LMManeuverPAD(opt, *form);
		sprintf(form->purpose, "PDI Abort");

		OrbMech::format_time_HHMMSS(GETbuffer, GETfromGMT(PZDKIT.Block[0].Display[1].ManGMT));
		OrbMech::format_time_HHMMSS(GETbuffer2, t_TPI_Abort);
		sprintf(form->remarks, "15 seconds at 10 percent, then full thrust. CSI time: %s, TPI time: %s, N equal to 1", GETbuffer, GETbuffer2);
	}
	break;
	case 75: //PRELIMINARY CSM BACKUP INSERTION UPDATE
		preliminary = true;
	case 76: //CSM BACKUP INSERTION UPDATE
	{
		AP11ManPADOpt opt;
		LambertMan lamopt;
		TwoImpulseResuls res;
		SV sv_CSM, sv_LM, sv_Ins;
		VECTOR3 dV_LVLH;
		double P30TIG;

		AP11MNV * form = (AP11MNV *)pad;

		sv_CSM = StateVectorCalc(calcParams.src);
		sv_LM = StateVectorCalc(calcParams.tgt);

		lamopt.axis = RTCC_LAMBERT_MULTIAXIS;
		lamopt.mode = 0;
		lamopt.N = 0;
		lamopt.Offset = -_V(-110.0*1852.0, 0.0, 14.7*1852.0);
		lamopt.Perturbation = RTCC_LAMBERT_PERTURBED;
		lamopt.sv_A = sv_CSM;
		lamopt.sv_P = sv_LM;
		lamopt.T1 = calcParams.Insertion + 3.0*60.0;
		lamopt.T2 = calcParams.CSI;

		LambertTargeting(&lamopt, res);
		PoweredFlightProcessor(sv_CSM, lamopt.T1, RTCC_ENGINETYPE_CSMSPS, 0.0, res.dV, false, P30TIG, dV_LVLH);

		opt.TIG = P30TIG;
		opt.dV_LVLH = dV_LVLH;
		opt.enginetype = RTCC_ENGINETYPE_CSMSPS;
		opt.HeadsUp = false;
		opt.REFSMMAT = GetREFSMMATfromAGC(&mcc->cm->agc.vagc, true);
		opt.RV_MCC = ConvertSVtoEphemData(sv_CSM);
		opt.WeightsTable = GetWeightsTable(calcParams.src, true, false);

		AP11ManeuverPAD(opt, *form);
		sprintf(form->purpose, "Backup Insertion");
		sprintf(form->remarks, "Four-jet ullage for 10 seconds");

		sv_Ins = ExecuteManeuver(sv_CSM, P30TIG, dV_LVLH, 0.0, RTCC_ENGINETYPE_CSMSPS);

		SPQOpt coeopt;
		SPQResults coeres;
		char GETbuffer[64], GETbuffer2[64];

		coeopt.DH = -15.0*1852.0;
		coeopt.E = 208.3*RAD;
		coeopt.sv_A = sv_Ins;
		coeopt.sv_P = sv_LM;
		coeopt.K_CDH = 1;
		coeopt.t_CSI = calcParams.CSI;

		ConcentricRendezvousProcessor(coeopt, coeres);

		OrbMech::format_time_HHMMSS(GETbuffer, calcParams.CSI);
		OrbMech::format_time_HHMMSS(GETbuffer2, coeres.t_TPI);
		sprintf(form->remarks, "CSI: %s, TPI: %s, N equals 1", GETbuffer, GETbuffer2);

		if (preliminary == false)
		{
			char buffer1[1000];

			AGCStateVectorUpdate(buffer1, sv_CSM, true);

			sprintf(uplinkdata, "%s", buffer1);
			if (upString != NULL) {
				// give to mcc
				strncpy(upString, uplinkdata, 1024 * 3);
				sprintf(upDesc, "CSM state vector");
			}
		}
	}
	break;
	case 77: //PRELIMINARY LM INSERTION UPDATE
		preliminary = true;
	case 78: //LM INSERTION UPDATE
	{
		AP11LMManPADOpt opt;
		LambertMan lamopt;
		TwoImpulseResuls res;
		SV sv_CSM, sv_LM;
		VECTOR3 dV_LVLH;
		double P30TIG;

		AP11LMMNV * form = (AP11LMMNV *)pad;

		sv_CSM = StateVectorCalc(calcParams.src);
		sv_LM = StateVectorCalc(calcParams.tgt);

		//Without descent stage
		LEM *lem = (LEM *)calcParams.tgt;
		sv_LM.mass = lem->GetAscentStageMass();

		lamopt.axis = RTCC_LAMBERT_MULTIAXIS;
		lamopt.mode = 0;
		lamopt.N = 0;
		lamopt.Offset = _V(-147.0*1852.0, 0.0, 14.7*1852.0);
		lamopt.Perturbation = RTCC_LAMBERT_PERTURBED;
		lamopt.sv_A = sv_LM;
		lamopt.sv_P = sv_CSM;
		lamopt.T1 = calcParams.Insertion;
		lamopt.T2 = calcParams.CSI;

		LambertTargeting(&lamopt, res);
		PoweredFlightProcessor(sv_LM, lamopt.T1, RTCC_ENGINETYPE_LMAPS, 0.0, res.dV, false, P30TIG, dV_LVLH);

		opt.TIG = P30TIG;
		opt.dV_LVLH = dV_LVLH;
		opt.enginetype = RTCC_ENGINETYPE_LMAPS;
		opt.HeadsUp = false;
		opt.REFSMMAT = GetREFSMMATfromAGC(&mcc->lm->agc.vagc, false);
		opt.RV_MCC = ConvertSVtoEphemData(sv_LM);
		opt.WeightsTable.CC[RTCC_CONFIG_A] = true;
		opt.WeightsTable.ConfigWeight = opt.WeightsTable.LMAscWeight = sv_LM.mass;

		AP11LMManeuverPAD(opt, *form);
		sprintf(form->purpose, "Insertion");

		if (preliminary == false)
		{
			char buffer1[1000];

			sprintf(form->remarks, "LM ascent stage weight is %.0lf", form->LMWeight);

			AGCStateVectorUpdate(buffer1, sv_CSM, true);
			sprintf(uplinkdata, "%s", buffer1);
			if (upString != NULL) {
				// give to mcc
				strncpy(upString, uplinkdata, 1024 * 3);
				sprintf(upDesc, "CSM state vector");
			}
		}
	}
	break;
	case 79: //CSI UPDATE
	{
		AP10CSIPADOpt manopt;
		SPQOpt opt;
		SPQResults res;
		SV sv_CSM, sv_LM;
		VECTOR3 dV_LVLH;
		double GETbase;
		PLAWDTOutput WeightsTable;

		AP10CSI * form = (AP10CSI *)pad;

		sv_CSM = StateVectorCalc(calcParams.src);
		sv_LM = StateVectorCalc(calcParams.tgt);
		WeightsTable = GetWeightsTable(calcParams.tgt, false, false);
		GETbase = CalcGETBase();

		opt.DH = 15.0*1852.0;
		opt.E = 26.6*RAD;
		opt.sv_A = sv_LM;
		opt.sv_P = sv_CSM;
		opt.K_CDH = 0;
		opt.t_CSI = calcParams.CSI;
		opt.t_TPI = calcParams.TPI;

		ConcentricRendezvousProcessor(opt, res);
		dV_LVLH = res.dV_CSI;

		//Use nominal AGS K-Factor for now
		SystemParameters.MCGZSS = SystemParameters.MCGZSL + 90.0;

		manopt.dV_LVLH = dV_LVLH;
		manopt.enginetype = RTCC_ENGINETYPE_LMAPS;
		manopt.REFSMMAT = GetREFSMMATfromAGC(&mcc->lm->agc.vagc, false);
		manopt.sv0 = ConvertSVtoEphemData(sv_LM);
		manopt.WeightsTable = WeightsTable;
		manopt.t_CSI = calcParams.CSI;
		manopt.t_TPI = calcParams.TPI;

		AP10CSIPAD(manopt, *form);
		form->type = 0;
	}
	break;
	case 80: //LM WEIGHT UPDATE
	{
		GENERICPAD * form = (GENERICPAD *)pad;

		double mass;

		mass = calcParams.tgt->GetMass();

		sprintf(form->paddata, "LM weight is %.0lf", mass / LBS2KG);
	}
	break;
	case 81: //APS DEPLETION UPDATE
	{
		AP11LMManPADOpt opt;
		SV sv, sv1, sv2;
		MATRIX3 Q_Xx;
		VECTOR3 UX, UY, UZ, DV, DV_P, DV_C, V_G, dV_LVLH;
		double GETbase, MJD_depletion, t_Depletion_guess, t_Depletion, dv, theta_T;
		char buffer1[1000];

		AP11LMMNV * form = (AP11LMMNV *)pad;

		sv = StateVectorCalc(calcParams.tgt);
		GETbase = CalcGETBase();
		EZJGMTX1.data[0].REFSMMAT = GetREFSMMATfromAGC(&mcc->cm->agc.vagc, true);
		EZJGMTX3.data[0].REFSMMAT = GetREFSMMATfromAGC(&mcc->lm->agc.vagc, false);

		t_Depletion_guess = OrbMech::HHMMSSToSS(108, 0, 0);
		dv = 4600.0*0.3048;

		sv1 = coast(sv, t_Depletion_guess - OrbMech::GETfromMJD(sv.MJD, GETbase));

		MJD_depletion = OrbMech::P29TimeOfLongitude(SystemParameters.MAT_J2000_BRCS, sv1.R, sv1.V, sv1.MJD, sv1.gravref, 0.0);
		t_Depletion = OrbMech::GETfromMJD(MJD_depletion, GETbase);
		sv2 = coast(sv1, t_Depletion - t_Depletion_guess);

		UY = unit(crossp(sv2.V, sv2.R));
		UZ = unit(-sv2.R);
		UX = crossp(UY, UZ);
		Q_Xx = _M(UX.x, UX.y, UX.z, UY.x, UY.y, UY.z, UZ.x, UZ.y, UZ.z);
		DV = UX * dv;
		DV_P = UX * dv;

		theta_T = -length(crossp(sv2.R, sv2.V))*dv*sv2.mass / OrbMech::power(length(sv2.R), 2.0) / APS_THRUST;
		DV_C = (unit(DV_P)*cos(theta_T / 2.0) + unit(crossp(DV_P, UY))*sin(theta_T / 2.0))*length(DV_P);
		V_G = DV_C;
		dV_LVLH = mul(Q_Xx, V_G);

		opt.TIG = t_Depletion;
		opt.dV_LVLH = dV_LVLH;
		opt.enginetype = RTCC_ENGINETYPE_LMAPS;
		opt.HeadsUp = false;
		opt.REFSMMAT = GetREFSMMATfromAGC(&mcc->lm->agc.vagc, false);
		opt.RV_MCC = ConvertSVtoEphemData(sv);
		opt.WeightsTable = GetWeightsTable(calcParams.tgt, false, false);

		AP11LMManeuverPAD(opt, *form);

		AGCStateVectorUpdate(buffer1, sv, false);

		DockAlignOpt dockopt;

		dockopt.LM_REFSMMAT = EZJGMTX3.data[0].REFSMMAT;
		dockopt.CSM_REFSMMAT = EZJGMTX1.data[0].REFSMMAT;
		dockopt.LMAngles = form->IMUAtt;
		dockopt.type = 3;

		DockingAlignmentProcessor(dockopt);
		sprintf(form->remarks, "CSM IMU angles. Roll %.0f, pitch %.0f, yaw %.0f", dockopt.CSMAngles.x*DEG, dockopt.CSMAngles.y*DEG, dockopt.CSMAngles.z*DEG);
		sprintf(form->purpose, "APS Depletion");

		sprintf(uplinkdata, "%s", buffer1);
		if (upString != NULL) {
			// give to mcc
			strncpy(upString, uplinkdata, 1024 * 3);
			sprintf(upDesc, "LM state vector");
		}
	}
	break;
	case 90: //MCC-5 UPDATE
	case 91: //PRELIMINARY MCC-6 UPDATE
	case 92: //MCC-6 UPDATE
	case 93: //MCC-7 DECISION
	case 94: //MCC-7 UPDATE
	{
		EntryOpt entopt;
		EntryResults res;
		AP11ManPADOpt opt;
		double MCCtime;
		MATRIX3 REFSMMAT;
		char manname[8];
		SV sv;

		AP11MNV * form = (AP11MNV *)pad;

		//Just so things don't break
		if (calcParams.TEI == 0)
		{
			calcParams.TEI = OrbMech::HHMMSSToSS(137, 20, 0);
		}
		if (calcParams.EI == 0)
		{
			calcParams.EI = OrbMech::HHMMSSToSS(191, 50, 0);
		}

		if (fcn == 90)
		{
			MCCtime = calcParams.TEI + 15.0*3600.0;
			sprintf(manname, "MCC-5");
		}
		else if (fcn == 91 || fcn == 92)
		{
			MCCtime = calcParams.EI - 15.0*3600.0;
			sprintf(manname, "MCC-6");
		}
		else if (fcn == 93 || fcn == 94)
		{
			MCCtime = calcParams.EI - 3.0*3600.0;
			sprintf(manname, "MCC-7");
		}

		sv = StateVectorCalc(calcParams.src); //State vector for uplink

		entopt.entrylongmanual = true;
		entopt.enginetype = RTCC_ENGINETYPE_CSMSPS;
		entopt.lng = -165.0*RAD;
		entopt.RV_MCC = sv;
		entopt.TIGguess = MCCtime;
		entopt.vessel = calcParams.src;
		entopt.type = 3; //Unspecified area

		//Calculate corridor control burn
		EntryTargeting(&entopt, &res);

		//If time to EI is more than 24 hours and the splashdown longitude is not within 2� of desired, then perform a longitude control burn
		if (MCCtime < calcParams.EI - 24.0*3600.0 && abs(res.longitude - entopt.lng) > 2.0*RAD)
		{
			entopt.type = 1;
			entopt.t_Z = res.GET400K;

			EntryTargeting(&entopt, &res);
		}

		//Apollo 10 Mission Rules
		if (MCCtime > calcParams.EI - 50.0*3600.0)
		{
			if (length(res.dV_LVLH) < 1.0*0.3048)
			{
				scrubbed = true;
			}
		}
		else
		{
			if (length(res.dV_LVLH) < 2.0*0.3048)
			{
				scrubbed = true;
			}
		}

		if (fcn == 94)
		{
			//MCC-7 update, calculate entry REFSMMAT

			REFSMMATOpt refsopt;
			refsopt.REFSMMATopt = 3;
			refsopt.vessel = calcParams.src;
			refsopt.useSV = true;
			refsopt.RV_MCC = res.sv_postburn;

			REFSMMAT = REFSMMATCalc(&refsopt);
		}
		else
		{
			REFSMMAT = GetREFSMMATfromAGC(&mcc->cm->agc.vagc, true);
		}

		if (scrubbed)
		{
			//Entry prediction without maneuver
			EntryUpdateCalc(sv, PZREAP.RRBIAS, true, &res);

			res.dV_LVLH = _V(0, 0, 0);
			res.P30TIG = entopt.TIGguess;
		}
		else
		{
			opt.WeightsTable = GetWeightsTable(calcParams.src, true, false);
			opt.TIG = res.P30TIG;
			opt.dV_LVLH = res.dV_LVLH;
			opt.enginetype = mcc->mcc_calcs.SPSRCSDecision(SPS_THRUST / opt.WeightsTable.ConfigWeight, res.dV_LVLH);
			opt.HeadsUp = true;
			opt.REFSMMAT = REFSMMAT;
			opt.RV_MCC = ConvertSVtoEphemData(sv);

			AP11ManeuverPAD(opt, *form);
			sprintf(form->purpose, manname);
			if (opt.enginetype == RTCC_ENGINETYPE_CSMSPS) sprintf(form->remarks, "Two-jet ullage for 14 seconds");
			form->lat = res.latitude*DEG;
			form->lng = res.longitude*DEG;
			form->RTGO = res.RTGO;
			form->VI0 = res.VIO / 0.3048;
			form->GET05G = res.GET05G;
		}

		//Uplink data
		if (scrubbed)
		{
			sprintf(upMessage, "%s has been scrubbed", manname);

			//Scrubbed MCC-5 and MCC-6
			if (fcn == 90 || fcn == 91 || fcn == 92)
			{
				char buffer1[1000];

				sprintf(upDesc, "CSM state vector and V66");

				AGCStateVectorUpdate(buffer1, sv, true, true);

				sprintf(uplinkdata, "%s", buffer1);
				if (upString != NULL) {
					// give to mcc
					strncpy(upString, uplinkdata, 1024 * 3);
				}
			}
			//Scrubbed MCC-7
			else if (fcn == 94)
			{
				char buffer1[1000];
				char buffer2[1000];
				char buffer3[1000];

				sprintf(upDesc, "CSM state vector and V66, entry target, Entry REFSMMAT");

				AGCStateVectorUpdate(buffer1, sv, true, true);
				CMCEntryUpdate(buffer2, res.latitude, res.longitude);
				AGCDesiredREFSMMATUpdate(buffer3, REFSMMAT);

				sprintf(uplinkdata, "%s%s%s", buffer1, buffer2, buffer3);
				if (upString != NULL) {
					// give to mcc
					strncpy(upString, uplinkdata, 1024 * 3);
				}
			}
		}
		else
		{
			//MCC-5 and MCC-6
			if (fcn == 90 || fcn == 92)
			{
				char buffer1[1000];
				char buffer2[1000];

				AGCStateVectorUpdate(buffer1, sv, true, true);
				CMCRetrofireExternalDeltaVUpdate(buffer2, res.latitude, res.longitude, res.P30TIG, res.dV_LVLH);

				sprintf(uplinkdata, "%s%s", buffer1, buffer2);
				if (upString != NULL) {
					// give to mcc
					strncpy(upString, uplinkdata, 1024 * 3);
					sprintf(upDesc, "CSM state vector and V66, target load");
				}
			}
			//MCC-6 (preliminary)
			else if (fcn == 91)
			{
				char buffer1[1000];

				AGCStateVectorUpdate(buffer1, sv, true, true);

				sprintf(uplinkdata, "%s", buffer1);
				if (upString != NULL) {
					// give to mcc
					strncpy(upString, uplinkdata, 1024 * 3);
					sprintf(upDesc, "CSM state vector and V66");
				}
			}
			//MCC-7 decision
			else if (fcn == 93)
			{
				sprintf(upMessage, "%s will be executed", manname);
			}
			//MCC-7
			else if (fcn == 94)
			{
				char buffer1[1000];
				char buffer2[1000];
				char buffer3[1000];

				AGCStateVectorUpdate(buffer1, sv, true, true);
				CMCRetrofireExternalDeltaVUpdate(buffer2, res.latitude, res.longitude, res.P30TIG, res.dV_LVLH);
				AGCDesiredREFSMMATUpdate(buffer3, REFSMMAT);

				sprintf(uplinkdata, "%s%s%s", buffer1, buffer2, buffer3);
				if (upString != NULL) {
					// give to mcc
					strncpy(upString, uplinkdata, 1024 * 3);
					sprintf(upDesc, "CSM state vector and V66, target load, Entry REFSMMAT");
				}
			}
		}

		//Save for further use
		calcParams.EI = res.GET400K;
		DeltaV_LVLH = res.dV_LVLH;
		TimeofIgnition = res.P30TIG;
		SplashLatitude = res.latitude;
		SplashLongitude = res.longitude;
		calcParams.SVSTORE1 = res.sv_postburn;
	}
	break;
	case 96: //ENTRY PAD (ASSUMES NO MCC-6, but MCC-7)
	case 97: //ENTRY PAD (ASSUMES MCC-6)
	case 98: //ENTRY PAD (ASSUMES MCC-7)
	case 99: //FINAL LUNAR ENTRY PAD
	{
		AP11ENT * form = (AP11ENT *)pad;

		SV sv;
		LunarEntryPADOpt entopt;
		MATRIX3 REFSMMAT;

		sv = StateVectorCalc(calcParams.src);

		if (length(DeltaV_LVLH) != 0.0 && fcn != 99)
		{
			entopt.direct = false;
		}
		else
		{
			entopt.direct = true;
		}

		if (fcn == 99)
		{
			REFSMMAT = GetREFSMMATfromAGC(&mcc->cm->agc.vagc, true);
		}
		else
		{
			REFSMMATOpt refsopt;
			refsopt.REFSMMATopt = 3;
			refsopt.vessel = calcParams.src;
			refsopt.useSV = true;
			refsopt.RV_MCC = calcParams.SVSTORE1;

			REFSMMAT = REFSMMATCalc(&refsopt);
		}

		entopt.dV_LVLH = DeltaV_LVLH;
		entopt.lat = SplashLatitude;
		entopt.lng = SplashLongitude;
		entopt.P30TIG = TimeofIgnition;
		entopt.REFSMMAT = REFSMMAT;
		entopt.sv0 = sv;

		LunarEntryPAD(entopt, *form);
		sprintf(form->Area[0], "MIDPAC");
		if (entopt.direct == false)
		{
			//Maneuver is performed
			if (fcn == 96 || fcn == 97)
			{
				//MCC-6
				sprintf(form->remarks[0], "Assumes MCC-6");
			}
			else if (fcn == 98)
			{
				//MCC-7
				sprintf(form->remarks[0], "Assumes MCC-7");
			}
		}
		else
		{
			//Maneuver scrubbed or final PAD
			if (fcn == 96 || fcn == 97)
			{
				//MCC-6
				//TBD: Calculate MCC-7
				sprintf(form->remarks[0], "Assumes MCC-7");
			}
		}

		if (fcn == 99)
		{
			//FINAL LUNAR ENTRY PAD
			char buffer1[1000];

			AGCStateVectorUpdate(buffer1, sv, true, true);

			sprintf(uplinkdata, "%s", buffer1);
			if (upString != NULL) {
				// give to mcc
				strncpy(upString, uplinkdata, 1024 * 3);
				sprintf(upDesc, "State vector and V66");
			}
		}
	}
	break;
	case 100: //GENERIC CSM STATE VECTOR UPDATE
	{
		SV sv;
		char buffer1[1000];

		sv = StateVectorCalc(calcParams.src); //State vector for uplink

		AGCStateVectorUpdate(buffer1, sv, true);

		sprintf(uplinkdata, "%s", buffer1);
		if (upString != NULL) {
			// give to mcc
			strncpy(upString, uplinkdata, 1024 * 3);
			sprintf(upDesc, "CSM state vector");
		}
	}
	break;
	case 101: //GENERIC CSM AND LM STATE VECTOR UPDATE
	{
		SV sv_CSM, sv_LM;
		char buffer1[1000];
		char buffer2[1000];

		sv_CSM = StateVectorCalc(calcParams.src);
		sv_LM = StateVectorCalc(calcParams.tgt);

		AGCStateVectorUpdate(buffer1, sv_CSM, true);
		AGCStateVectorUpdate(buffer2, sv_LM, false);

		sprintf(uplinkdata, "%s%s", buffer1, buffer2);
		if (upString != NULL) {
			// give to mcc
			strncpy(upString, uplinkdata, 1024 * 3);
			sprintf(upDesc, "CSM and LM state vectors");
		}
	}
	break;
	case 102: //GENERIC LM STATE VECTOR UPDATE
	{
		SV sv;
		char buffer1[1000];

		sv = StateVectorCalc(calcParams.tgt); //State vector for uplink

		AGCStateVectorUpdate(buffer1, sv, false);

		sprintf(uplinkdata, "%s", buffer1);
		if (upString != NULL) {
			// give to mcc
			strncpy(upString, uplinkdata, 1024 * 3);
			sprintf(upDesc, "LM state vector");
		}
	}
	break;
	case 103: //GENERIC CMC CSM STATE VECTOR UPDATE AND V66
	{
		EphemerisData sv;
		char buffer1[1000];

		sv = StateVectorCalcEphem(calcParams.src); //State vector for uplink

		AGCStateVectorUpdate(buffer1, 1, 1, sv, true);

		sprintf(uplinkdata, "%s", buffer1);
		if (upString != NULL) {
			// give to mcc
			strncpy(upString, uplinkdata, 1024 * 3);
			sprintf(upDesc, "CSM state vector and V66");
		}
	}
	break;
	case 200: //LLS2 Photo PAD
	case 202: //LLS3 Photo PAD
	{
		GENERICPAD * form = (GENERICPAD *)pad;
		char buffer1[1000], buffer2[1000], buffer3[1000], LS_ID[32];

		//Calculate T2 as time of closest approach
		//T1 is 2 minutes earlier
		//T0 might be at a specific longitude, but for now it's at a fixed time prior to T2 as well

		EphemerisDataTable2 ephem;
		EphemerisData sv0;
		double get_guess, gmt_guess, T0, T1, T2, lng;

		if (fcn == 200)
		{
			//LLS2
			get_guess = OrbMech::HHMMSSToSS(118, 0, 0);
			lng = 23.65*RAD;
			sprintf(LS_ID, "LLS 2");
		}
		else
		{
			//LLS3
			get_guess = OrbMech::HHMMSSToSS(132, 0, 0);
			lng = -1.35*RAD;
			sprintf(LS_ID, "LLS 3");
		}
		gmt_guess = GMTfromGET(get_guess);
		
		sv0 = StateVectorCalcEphem(calcParams.src);
		mcc->mcc_calcs.CreateEphemeris(sv0, gmt_guess, gmt_guess + 4.0*3600.0, ephem);

		mcc->mcc_calcs.LongitudeCrossing(ephem, lng, gmt_guess, T2);

		T2 = GETfromGMT(T2);
		T1 = T2 - 120.0;
		T0 = T1 - (4.0*60.0 + 23.0);

		OrbMech::format_time_HHMMSS(buffer1, T0);
		OrbMech::format_time_HHMMSS(buffer2, T1);
		OrbMech::format_time_HHMMSS(buffer3, T2);

		sprintf(form->paddata, "OBLIQUE STRIP %s  T0 %s  T1 %s  T2 %s", LS_ID, buffer1, buffer2, buffer3);
	}
	break;
	case 201: //Strip Photo Update (rev 22)
	{
		GENERICPAD * form = (GENERICPAD *)pad;

		EphemerisDataTable2 ephem;
		EphemerisData sv0;
		double gmt_guess, T0, T1, T2, T3;
		char buffer1[1000], buffer2[1000], buffer3[1000], buffer4[1000];

		gmt_guess = GMTfromGET(OrbMech::HHMMSSToSS(119, 0, 0));

		sv0 = StateVectorCalcEphem(calcParams.src);
		mcc->mcc_calcs.CreateEphemeris(sv0, gmt_guess, gmt_guess + 4.0*3600.0, ephem);

		//T0: Camera start, at terminator rise
		T0 = mcc->mcc_calcs.TerminatorRise(ephem, gmt_guess);

		//T1: Sub-solar point (TBD)
		T1 = T0 + 28.0*60.0;

		//T2: 65�E crossing
		mcc->mcc_calcs.LongitudeCrossing(ephem, 65.0*RAD, T1, T2);
		
		//T3: 34�E crossing
		mcc->mcc_calcs.LongitudeCrossing(ephem, 34.0*RAD, T2, T3);

		//Convert to GET
		T0 = GETfromGMT(T0);
		T1 = GETfromGMT(T1);
		T2 = GETfromGMT(T2);
		T3 = GETfromGMT(T3);

		OrbMech::format_time_HHMMSS(buffer1, T0);
		OrbMech::format_time_HHMMSS(buffer2, T1);
		OrbMech::format_time_HHMMSS(buffer3, T2);
		OrbMech::format_time_HHMMSS(buffer4, T3);

		sprintf(form->paddata, "VERTICAL STERO  T0 %s Camera start  T1 %s (Sub-solar pt)  T2 %s (65�E)  T3 %s (34�E)", buffer1, buffer2, buffer3, buffer4);
	}
	break;
	case 203: //Strip Photo Update (rev 31)
	{
		GENERICPAD * form = (GENERICPAD *)pad;

		EphemerisDataTable2 ephem;
		EphemerisData sv0;
		double gmt_guess, T0, T1, T2;
		char buffer1[1000], buffer2[1000], buffer3[1000];

		gmt_guess = GMTfromGET(OrbMech::HHMMSSToSS(135, 0, 0));

		sv0 = StateVectorCalcEphem(calcParams.src);
		mcc->mcc_calcs.CreateEphemeris(sv0, gmt_guess, gmt_guess + 4.0*3600.0, ephem);

		//T0: 90�E crossing
		mcc->mcc_calcs.LongitudeCrossing(ephem, 90.0*RAD, gmt_guess, T0);

		//T1: 85�E crossing
		mcc->mcc_calcs.LongitudeCrossing(ephem, 85.0*RAD, T0, T1);

		//T2: 30�E crossing
		mcc->mcc_calcs.LongitudeCrossing(ephem, 30.0*RAD, T1, T2);

		//Convert to GET
		T0 = GETfromGMT(T0);
		T1 = GETfromGMT(T1);
		T2 = GETfromGMT(T2);

		OrbMech::format_time_HHMMSS(buffer1, T0);
		OrbMech::format_time_HHMMSS(buffer2, T1);
		OrbMech::format_time_HHMMSS(buffer3, T2);

		sprintf(form->paddata, "DESCENT STRIP AND LLS3  T0 %s  T1 %s (85�E)  T2 %s (30�E)", buffer1, buffer2, buffer3);
	}
	break;
	case 204: //TV UPDATE
	{
		GENERICPAD * form = (GENERICPAD *)pad;

		sprintf(form->paddata, "TV UPDATE  R 180 HGA  P 293 P -58  Y 000 Y 005");
	}
	break;
	}

	return scrubbed;
}