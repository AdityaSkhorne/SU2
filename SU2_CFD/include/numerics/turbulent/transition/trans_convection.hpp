/*!
 * \file trans_convection.hpp
 * \brief Delarations of numerics classes for discretization of
 *        convective fluxes in transition problems.
 * \author S. Kang
 * \version 8.1.0 "Harrier"
 *
 * SU2 Project Website: https://su2code.github.io
 *
 * The SU2 Project is maintained by the SU2 Foundation
 * (http://su2foundation.org)
 *
 * Copyright 2012-2024, SU2 Contributors (cf. AUTHORS.md)
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

#pragma once

#include "../turb_convection.hpp"

/*!
 * \class CUpwSca_TransLM
 * \brief Re-use the SST convective fluxes for the scalar upwind discretization of LM transition model equations.
 * \ingroup ConvDiscr
 */
template <class FlowIndices>
using CUpwSca_TransLM  = CUpwSca_TurbSST<FlowIndices>;

/*!
 * \class CUpwSca_TransSLM
 * \brief Re-use the SA convective fluxes for the scalar upwind discretization of Simplified LM transition model equations.
 * \ingroup ConvDiscr
 */
template <class FlowIndices>
using CUpwSca_TransSLM  = CUpwSca_TurbSA<FlowIndices>;
