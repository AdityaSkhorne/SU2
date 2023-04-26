/*!
 * \file CfluidFlamelet.cpp
 * \brief Main subroutines of CFluidFlamelet class
 * \author D. Mayer, T. Economon, N. Beishuizen
 * \version 7.5.1 "Blackbird"
 *
 * SU2 Project Website: https://su2code.github.io
 *
 * The SU2 Project is maintained by the SU2 Foundation
 * (http://su2foundation.org)
 *
 * Copyright 2012-2023, SU2 Contributors (cf. AUTHORS.md)
 *
 * SU2 is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * SU2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with SU2. If not, see <http://www.gnu.org/licenses/>.
 */

#include "../include/fluid/CFluidFlamelet.hpp"
#include "../../../Common/include/containers/CLookUpTable.hpp"
#if defined(HAVE_MLPCPP)
#include "../../../subprojects/MLPCpp/include/CLookUp_ANN.hpp"
#define USE_MLPCPP
#endif

CFluidFlamelet::CFluidFlamelet(CConfig* config, su2double value_pressure_operating, bool display) : CFluidModel() {
  rank = SU2_MPI::GetRank();
  /* -- number of auxiliary species transport equations, e.g. 1=CO, 2=NOx  --- */
  n_user_scalars = config->GetNUserScalars();
  n_control_vars = config->GetNControlVars();
  n_scalars = config->GetNScalars();
  //PreferentialDiffusion = config->GetPreferentialDiffusion();

  if (rank == MASTER_NODE) {
    cout << "Number of scalars:           " << n_scalars << endl;
    cout << "Number of user scalars:      " << n_user_scalars << endl;
    cout << "Number of control variables: " << n_control_vars << endl;
  }

  controlling_variables.resize(n_control_vars);

  n_datadriven_inputs = config->GetNDataDriven_Files();

  include_mixfrac = (n_control_vars > 2);

  controlling_variables[I_PROGVAR] = "ProgressVariable";
  controlling_variables[I_ENTH] = "EnthalpyTot";
  if (include_mixfrac) controlling_variables[I_MIXFRAC] = "MixtureFraction";

  table_scalar_names.resize(n_scalars);
  for (size_t i_CV = 0; i_CV < n_control_vars; i_CV++) table_scalar_names[i_CV] = controlling_variables[i_CV];
  /*--- auxiliary species transport equations---*/
  for (size_t i_aux = 0; i_aux < n_user_scalars; i_aux++) {
    table_scalar_names[n_control_vars + i_aux] = config->GetUserScalarName(i_aux);
  }

  manifold_format = config->GetKind_DataDriven_Method();
  switch (manifold_format) {
    case ENUM_DATADRIVEN_METHOD::LUT:
      if (rank == MASTER_NODE) {
        cout << "*****************************************" << endl;
        cout << "***   initializing the lookup table   ***" << endl;
        cout << "*****************************************" << endl;
      }
      look_up_table = new CLookUpTable(config->GetDataDriven_FileNames()[0], table_scalar_names[I_PROGVAR],
                                       table_scalar_names[I_ENTH]);
      if (look_up_table->GetNDim() != n_control_vars)
        SU2_MPI::Error("Mismatch between table dimension and number of controlling variables.", CURRENT_FUNCTION);
      break;

    case ENUM_DATADRIVEN_METHOD::MLP:
#ifdef USE_MLPCPP
      if ((rank == MASTER_NODE) && display) {
        cout << "***********************************************" << endl;
        cout << "*** initializing the multi-layer perceptron ***" << endl;
        cout << "***********************************************" << endl;
      }
      look_up_ANN = new MLPToolbox::CLookUp_ANN(n_datadriven_inputs, config->GetDataDriven_FileNames());
      if ((rank == MASTER_NODE) && display) look_up_ANN->DisplayNetworkInfo();
#else
      SU2_MPI::Error("SU2 was not compiled with MLPCpp enabled (-Denable-mlpcpp=true).", CURRENT_FUNCTION);
#endif
      break;
    default:
      break;
  }

  config->SetLUTScalarNames(table_scalar_names);

  /*--- we currently only need 1 source term from the LUT for the progress variable
        and each auxiliary equations needs 2 source terms ---*/
  n_table_sources = 1 + 2 * n_user_scalars;

  table_source_names.resize(n_table_sources);
  table_sources.resize(n_table_sources);
  table_source_names[I_SRC_TOT_PROGVAR] = "ProdRateTot_PV";
  /*--- No source term for enthalpy ---*/

  /*--- For the auxiliary equations, we use a positive (production) and a negative (consumption) term:
        S_tot = S_PROD + S_CONS * Y ---*/

  for (size_t i_aux = 0; i_aux < n_user_scalars; i_aux++) {
    /*--- Order of the source terms: S_prod_1, S_cons_1, S_prod_2, S_cons_2, ...---*/
    table_source_names[1 + 2 * i_aux] = config->GetUserSourceName(2 * i_aux);
    table_source_names[1 + 2 * i_aux + 1] = config->GetUserSourceName(2 * i_aux + 1);
  }

  config->SetLUTSourceNames(table_source_names);

  n_lookups = config->GetNLookups();
  table_lookup_names.resize(n_lookups);
  for (int i_lookup = 0; i_lookup < n_lookups; ++i_lookup) {
    table_lookup_names[i_lookup] = config->GetLUTLookupName(i_lookup);
  }

  source_scalar.resize(n_scalars);
  lookup_scalar.resize(n_lookups);

  Pressure = value_pressure_operating;

  PreprocessLookUp();

  config->SetPreferentialDiffusion(PreferentialDiffusion);

  if (rank == MASTER_NODE) {
    cout << "Preferential Diffusion: " << (PreferentialDiffusion ? "Enabled" : "Disabled") << endl << endl;
  }
}

CFluidFlamelet::~CFluidFlamelet() {
  switch (manifold_format) {
    case ENUM_DATADRIVEN_METHOD::LUT:
      delete look_up_table;
      break;
    case ENUM_DATADRIVEN_METHOD::MLP:
#ifdef USE_MLPCPP
      delete iomap_TD;
      if (PreferentialDiffusion) delete iomap_PD;
      delete iomap_Sources;
      delete iomap_LookUp;
      delete look_up_ANN;
#endif
    default:
      break;
  }
}

/*--- do a lookup for the list of variables in table_lookup_names, for visualization purposes ---*/
unsigned long CFluidFlamelet::SetScalarLookups(const su2double* val_scalars) {
  su2double enth = val_scalars[I_ENTH];
  su2double prog = val_scalars[I_PROGVAR];

  string name_enth = table_scalar_names[I_ENTH];
  string name_prog = table_scalar_names[I_PROGVAR];

  val_controlling_vars[I_PROGVAR] = prog;
  val_controlling_vars[I_ENTH] = enth;

#ifdef USE_MLPCPP
  iomap_current = iomap_LookUp;
#endif
  /*--- add all quantities and their address to the look up vectors ---*/
  unsigned long exit_code = Evaluate_Dataset(varnames_LookUp, val_vars_LookUp);

  return exit_code;
}

/*--- set the source terms for the transport equations ---*/
unsigned long CFluidFlamelet::SetScalarSources(const su2double* val_scalars) {
  table_sources[0] = 0.0;

  val_controlling_vars[I_PROGVAR] = val_scalars[I_PROGVAR];
  val_controlling_vars[I_ENTH] = val_scalars[I_ENTH];
  if (PreferentialDiffusion) val_controlling_vars[I_MIXFRAC] = val_scalars[I_MIXFRAC];
#ifdef USE_MLPCPP
  iomap_current = iomap_Sources;
#endif
  /*--- add all quantities and their address to the look up vectors ---*/
  unsigned long exit_code = Evaluate_Dataset(varnames_Sources, val_vars_Sources);

  /*--- The source term for progress variable is always positive, we clip from below to makes sure. --- */
  source_scalar[I_PROGVAR] = max(EPS, table_sources[I_SRC_TOT_PROGVAR]);
  source_scalar[I_ENTH] = 0.0;
  if (PreferentialDiffusion) source_scalar[I_MIXFRAC] = 0.0;

  /*--- Source term for the auxiliary species transport equations ---*/
  for (size_t i_aux = 0; i_aux < n_user_scalars; i_aux++) {
    /*--- The source term for the auxiliary equations consists of a production term and a consumption term:
          S_TOT = S_PROD + S_CONS * Y ---*/
    su2double y_aux = val_scalars[n_control_vars + i_aux];
    su2double source_prod = table_sources[1 + 2 * i_aux];
    su2double source_cons = table_sources[1 + 2 * i_aux + 1];
    source_scalar[n_control_vars + i_aux] = source_prod + source_cons * y_aux;
  }

  return exit_code;
}

void CFluidFlamelet::SetTDState_T(su2double val_temperature, const su2double* val_scalars) {
  val_controlling_vars[I_PROGVAR] = val_scalars[I_PROGVAR];
  val_controlling_vars[I_ENTH] = val_scalars[I_ENTH];
  if (PreferentialDiffusion) val_controlling_vars[I_MIXFRAC] = val_scalars[I_MIXFRAC];
#ifdef USE_MLPCPP
  iomap_current = iomap_TD;
#endif
  /*--- add all quantities and their address to the look up vectors ---*/
  Evaluate_Dataset(varnames_TD, val_vars_TD);

  /*--- compute Cv from Cp and molar weight of the mixture (ideal gas) ---*/
  Cv = Cp - UNIVERSAL_GAS_CONSTANT / (molar_weight);

  Density = Pressure * (molar_weight / 1000.0) / (UNIVERSAL_GAS_CONSTANT * Temperature);

  // mass_diffusivity = Kt / (Density * Cp);
}

unsigned long CFluidFlamelet::SetPreferentialDiffusionScalars(su2double* val_scalars) {
  val_controlling_vars[I_PROGVAR] = val_scalars[I_PROGVAR];
  val_controlling_vars[I_ENTH] = val_scalars[I_ENTH];
  if (PreferentialDiffusion) val_controlling_vars[I_MIXFRAC] = val_scalars[I_MIXFRAC];
#ifdef USE_MLPCPP
  iomap_current = iomap_PD;
#endif
  /*--- add all quantities and their address to the look up vectors ---*/
  unsigned long exit_code = Evaluate_Dataset(varnames_PD, val_vars_PD);
  return exit_code;
}

unsigned long CFluidFlamelet::GetEnthFromTemp(su2double& val_enth, const su2double val_prog,
                                              const su2double val_mixfrac, const su2double val_temp,
                                              su2double initial_value) {
  /*--- convergence criterion for temperature in [K], high accuracy needed for restarts. ---*/
  su2double delta_temp_final = 0.001;
  su2double enth_iter = initial_value;
  su2double delta_enth;
  su2double delta_temp_iter = 1e10;

  unsigned long exit_code = 0, counter_limit = 50, counter = 0;

  bool converged = false;
#ifdef USE_MLPCPP
  iomap_current = iomap_TD;
#endif

  val_controlling_vars[I_PROGVAR] = val_prog;
  if (PreferentialDiffusion) val_controlling_vars[I_MIXFRAC] = val_mixfrac;

  while (!converged && (counter++ < counter_limit)) {
    val_controlling_vars[I_ENTH] = enth_iter;
    /*--- look up temperature and heat capacity ---*/
    Evaluate_Dataset(varnames_TD, val_vars_TD);
    /*--- calculate delta_temperature ---*/
    delta_temp_iter = val_temp - Temperature;

    if (abs(delta_temp_iter)/val_temp < 1e-6) {
      converged = true;
    } else {
      /* calculate delta_enthalpy following dh = cp * dT */
      delta_enth = Cp * delta_temp_iter;

      /*--- update enthalpy ---*/
      enth_iter += delta_enth;
    }
  }

  val_enth = enth_iter;

  if (counter >= counter_limit) {
    exit_code = 1;
  }
  return exit_code;
}

su2double CFluidFlamelet::GetBurntProgVar(su2double val_mixfrac) const {
  su2double pv_burnt;
  switch (manifold_format) {
    case ENUM_DATADRIVEN_METHOD::LUT:
    if (PreferentialDiffusion) {
      auto inclusion_levels = look_up_table->FindInclusionLevels(val_mixfrac);
      auto pv_bounds_lower = look_up_table->GetTableLimitsX(inclusion_levels.first);
      auto pv_bounds_upper = look_up_table->GetTableLimitsX(inclusion_levels.second);
      pv_burnt = 0.5*(pv_bounds_lower.second + pv_bounds_upper.second);
    } else {
      auto pv_bounds = look_up_table->GetTableLimitsX();
      pv_burnt = pv_bounds.second;
    }
    break;
    case ENUM_DATADRIVEN_METHOD::MLP:
#ifdef USE_MLPCPP
    auto pv_bounds = look_up_ANN->GetInputNorm(iomap_TD, I_PROGVAR);
    pv_burnt = pv_bounds.second;
#endif
    break;
  }
  return pv_burnt;
}
void CFluidFlamelet::PreprocessLookUp() {
  /*--- Set lookup names and variables for all relevant lookup processes in the fluid model. ---*/

  val_controlling_vars.resize(n_control_vars);

  /*--- Thermodynamic state variables ---*/
  size_t n_TD = 6;
  varnames_TD.resize(n_TD);
  val_vars_TD.resize(n_TD);

  /*--- The string in varnames_TD as it appears in the LUT file. ---*/
  varnames_TD[0] = "Temperature";
  val_vars_TD[0] = &Temperature;
  varnames_TD[1] = "mean_molar_weight";
  val_vars_TD[1] = &molar_weight;
  varnames_TD[2] = "Cp";
  val_vars_TD[2] = &Cp;
  varnames_TD[3] = "ViscosityDyn";
  val_vars_TD[3] = &Mu;
  varnames_TD[4] = "Conductivity";
  val_vars_TD[4] = &Kt;
  varnames_TD[5] = "DiffusionCoefficient";
  val_vars_TD[5] = &mass_diffusivity;

  /*--- Source term variables ---*/
  varnames_Sources.resize(n_table_sources);
  val_vars_Sources.resize(n_table_sources);

  for (size_t iSource = 0; iSource < n_table_sources; iSource++) {
    varnames_Sources[iSource] = table_source_names[iSource];
    val_vars_Sources[iSource] = &table_sources[iSource];
  }

  /*--- Passive lookups ---*/
  varnames_LookUp.resize(n_lookups);
  val_vars_LookUp.resize(n_lookups);

  for (size_t iLookup = 0; iLookup < n_lookups; iLookup++) {
    varnames_LookUp[iLookup] = table_lookup_names[iLookup];
    val_vars_LookUp[iLookup] = &lookup_scalar[iLookup];
  }

  varnames_CV.resize(n_control_vars);
  val_vars_CV.resize(n_control_vars);
  lookup_CV.resize(n_control_vars);
  for (auto iCV = 0u; iCV < n_control_vars; iCV++) {
    varnames_CV[iCV] = controlling_variables[iCV];
    val_vars_CV[iCV] = &lookup_CV[iCV];
  }

  varnames_PD.resize(FLAMELET_PREF_DIFF_SCALARS::N_BETA_TERMS);
  val_vars_PD.resize(FLAMELET_PREF_DIFF_SCALARS::N_BETA_TERMS);

  varnames_PD[FLAMELET_PREF_DIFF_SCALARS::I_BETA_PROGVAR] = "Beta_ProgVar";
  varnames_PD[FLAMELET_PREF_DIFF_SCALARS::I_BETA_ENTH_THERMAL] = "Beta_Enth_Thermal";
  varnames_PD[FLAMELET_PREF_DIFF_SCALARS::I_BETA_ENTH] = "Beta_Enth";
  varnames_PD[FLAMELET_PREF_DIFF_SCALARS::I_BETA_MIXFRAC] = "Beta_MixFrac";

  val_vars_PD[FLAMELET_PREF_DIFF_SCALARS::I_BETA_PROGVAR] = &beta_progvar;
  val_vars_PD[FLAMELET_PREF_DIFF_SCALARS::I_BETA_ENTH_THERMAL] = &beta_enth_thermal;
  val_vars_PD[FLAMELET_PREF_DIFF_SCALARS::I_BETA_ENTH] = &beta_enth;
  val_vars_PD[FLAMELET_PREF_DIFF_SCALARS::I_BETA_MIXFRAC] = &beta_mixfrac;


  size_t n_betas {0};
  PreferentialDiffusion = false;
  switch (manifold_format)
  {
  case ENUM_DATADRIVEN_METHOD::LUT:
    PreferentialDiffusion = look_up_table->CheckForVariables(varnames_PD);
    break;
  case ENUM_DATADRIVEN_METHOD::MLP:
  #ifdef USE_MLPCPP
    for (auto iMLP=0u; iMLP < n_datadriven_inputs; iMLP++) {
      auto outputMap = look_up_ANN->FindVariableIndices(iMLP, varnames_PD, false);
      n_betas += outputMap.size();
    }
    PreferentialDiffusion = (n_betas == varnames_PD.size());
  #endif
    break;
  default:
    break;
  }

  if (PreferentialDiffusion && !include_mixfrac) 
    SU2_MPI::Error("Preferential diffusion can only be used with mixture fraction as a controlling variable.", CURRENT_FUNCTION);


  if (manifold_format == ENUM_DATADRIVEN_METHOD::MLP) {
#ifdef USE_MLPCPP
    iomap_TD = new MLPToolbox::CIOMap(controlling_variables, varnames_TD);
    look_up_ANN->PairVariableswithMLPs(*iomap_TD);
    iomap_Sources = new MLPToolbox::CIOMap(controlling_variables, varnames_Sources);
    look_up_ANN->PairVariableswithMLPs(*iomap_Sources);
    iomap_LookUp = new MLPToolbox::CIOMap(controlling_variables, varnames_LookUp);
    look_up_ANN->PairVariableswithMLPs(*iomap_LookUp);
    if (PreferentialDiffusion){
      iomap_PD = new MLPToolbox::CIOMap(controlling_variables, varnames_PD);
      look_up_ANN->PairVariableswithMLPs(*iomap_PD);
    }
#endif
  }
}

unsigned long CFluidFlamelet::Evaluate_Dataset(vector<string>& varnames, vector<su2double*>& val_vars) {
  unsigned long exit_code = 0;

  vector<string> LUT_varnames;
  vector<su2double*> LUT_val_vars;
  su2matrix<su2double*> gradient_refs;

  su2vector<su2double> CV_LUT;
  CV_LUT.resize(n_control_vars);
  switch (manifold_format) {
    case ENUM_DATADRIVEN_METHOD::LUT:
      LUT_varnames.resize(varnames.size());
      LUT_val_vars.resize(val_vars.size());
      for (auto iVar = 0u; iVar < varnames.size(); iVar++) {
        LUT_varnames[iVar] = varnames[iVar];
        LUT_val_vars[iVar] = val_vars[iVar];
      }
      if (PreferentialDiffusion) {
        exit_code = look_up_table->LookUp_XYZ(LUT_varnames, LUT_val_vars, val_controlling_vars[I_PROGVAR],
                                              val_controlling_vars[I_ENTH], val_controlling_vars[I_MIXFRAC]);
      } else
        exit_code = look_up_table->LookUp_XY(LUT_varnames, LUT_val_vars, val_controlling_vars[I_PROGVAR],
                                             val_controlling_vars[I_ENTH]);

      break;
    case ENUM_DATADRIVEN_METHOD::MLP:
#ifdef USE_MLPCPP
      exit_code = look_up_ANN->PredictANN(iomap_current, val_controlling_vars, val_vars);
#endif
      break;
    default:
      break;
  }
  return exit_code;
}
