/*!
 * \file matrix_structure.cpp
 * \brief Main subroutines for doing the sparse structures
 * \author F. Palacios, A. Bueno, T. Economon
 * \version 7.0.0 "Blackbird"
 *
 * SU2 Project Website: https://su2code.github.io
 *
 * The SU2 Project is maintained by the SU2 Foundation
 * (http://su2foundation.org)
 *
 * Copyright 2012-2019, SU2 Contributors (cf. AUTHORS.md)
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

#include "../../include/linear_algebra/CSysMatrix.inl"

template<class ScalarType>
CSysMatrix<ScalarType>::CSysMatrix(void) {

  size = SU2_MPI::GetSize();
  rank = SU2_MPI::GetRank();

  matrix            = nullptr;
  row_ptr           = nullptr;
  dia_ptr           = nullptr;
  col_ind           = nullptr;

  ILU_matrix        = nullptr;
  row_ptr_ilu       = nullptr;
  dia_ptr_ilu       = nullptr;
  col_ind_ilu       = nullptr;

  invM              = nullptr;

  block             = nullptr;
  block_weight      = nullptr;
  block_inverse     = nullptr;

  prod_row_vector   = nullptr;
  aux_vector        = nullptr;
  sum_vector        = nullptr;

#ifdef USE_MKL
  MatrixMatrixProductJitter              = nullptr;
  MatrixVectorProductJitterBetaOne       = nullptr;
  MatrixVectorProductJitterBetaZero      = nullptr;
  MatrixVectorProductJitterAlphaMinusOne = nullptr;
  MatrixVectorProductTranspJitterBetaOne = nullptr;
  mkl_ipiv = nullptr;
#endif

}

template<class ScalarType>
CSysMatrix<ScalarType>::~CSysMatrix(void) {

  if (matrix     != nullptr)  delete [] matrix;
  if (ILU_matrix != nullptr)  delete [] ILU_matrix;
  if (invM       != nullptr)  delete [] invM;

  if (block         != nullptr)  delete [] block;
  if (block_weight  != nullptr)  delete [] block_weight;
  if (block_inverse != nullptr)  delete [] block_inverse;

  if (prod_row_vector != nullptr)  delete [] prod_row_vector;
  if (aux_vector      != nullptr)  delete [] aux_vector;
  if (sum_vector      != nullptr)  delete [] sum_vector;

#ifdef USE_MKL
  if ( MatrixMatrixProductJitter != nullptr )              mkl_jit_destroy( MatrixMatrixProductJitter );
  if ( MatrixVectorProductJitterBetaZero != nullptr )      mkl_jit_destroy( MatrixVectorProductJitterBetaZero );
  if ( MatrixVectorProductJitterBetaOne  != nullptr )      mkl_jit_destroy( MatrixVectorProductJitterBetaOne );
  if ( MatrixVectorProductJitterAlphaMinusOne != nullptr ) mkl_jit_destroy( MatrixVectorProductJitterAlphaMinusOne );
  if ( MatrixVectorProductTranspJitterBetaOne != nullptr ) mkl_jit_destroy( MatrixVectorProductTranspJitterBetaOne );
  if ( mkl_ipiv != nullptr ) delete [] mkl_ipiv;
#endif

}

template<class ScalarType>
void CSysMatrix<ScalarType>::Initialize(unsigned long npoint, unsigned long npointdomain,
                            unsigned short nvar, unsigned short neqn,
                            bool EdgeConnect, CGeometry *geometry, CConfig *config) {

  if(matrix != nullptr)
    SU2_MPI::Error("CSysMatrix can only be initialized once.", CURRENT_FUNCTION);

  /*--- Application of this matrix, FVM or FEM. ---*/
  auto type = EdgeConnect? ConnectivityType::FiniteVolume : ConnectivityType::FiniteElement;

  /*--- Types of preconditioner the matrix will be asked to build. ---*/
  unsigned short sol_prec = config->GetKind_Linear_Solver_Prec();
  unsigned short def_prec = config->GetKind_Deform_Linear_Solver_Prec();
  unsigned short adj_prec = config->GetKind_DiscAdj_Linear_Prec();
  bool adjoint = config->GetDiscrete_Adjoint();

  bool ilu_needed = (sol_prec==ILU) || (def_prec==ILU) || (adjoint && (adj_prec==ILU));

  /*--- Basic dimensions. ---*/
  nVar = nvar;
  nEqn = neqn;
  nPoint = npoint;
  nPointDomain = npointdomain;

  /*--- Get sparse structure pointers from geometry,
   *    the data is managed by CGeometry to allow re-use. ---*/

  const auto& csr = geometry->GetSparsePattern(type,0);

  row_ptr = csr.outerPtr();
  col_ind = csr.innerIdx();
  dia_ptr = csr.diagPtr();
  nnz = csr.getNumNonZeros();

  /*--- Get ILU sparse pattern, if fill is 0 no new data is allocated. --*/

  if(ilu_needed)
  {
    ilu_fill_in = config->GetLinear_Solver_ILU_n();

    const auto& csr_ilu = geometry->GetSparsePattern(type, ilu_fill_in);

    row_ptr_ilu = csr_ilu.outerPtr();
    col_ind_ilu = csr_ilu.innerIdx();
    dia_ptr_ilu = csr_ilu.diagPtr();
    nnz_ilu = csr_ilu.getNumNonZeros();
  }

  /*--- Allocate data. ---*/

#define ALLOC_AND_INIT(ptr,sz) {\
  ptr = new ScalarType [sz]; for(size_t k=0; k<sz; ++k) ptr[k]=0.0; }

  ALLOC_AND_INIT(matrix, nnz*nVar*nEqn)

  ALLOC_AND_INIT(block, nVar*nEqn)
  ALLOC_AND_INIT(block_weight, nVar*nEqn)
  ALLOC_AND_INIT(block_inverse, nVar*nEqn)

  ALLOC_AND_INIT(aux_vector, nVar)
  ALLOC_AND_INIT(sum_vector, nVar)
  ALLOC_AND_INIT(prod_row_vector, nVar)

  /*--- Preconditioners. ---*/

  if (ilu_needed) {
    ALLOC_AND_INIT(ILU_matrix, nnz_ilu*nVar*nEqn)
  }

  if (ilu_needed || (sol_prec==JACOBI) || (sol_prec==LINELET) ||
      (adjoint && (adj_prec==JACOBI)) || (def_prec==JACOBI))
  {
    ALLOC_AND_INIT(invM, nPointDomain*nVar*nEqn);
  }
#undef ALLOC_AND_INIT

  /*--- Generate MKL Kernels ---*/

#ifdef USE_MKL
  mkl_jit_create_dgemm( &MatrixMatrixProductJitter, MKL_ROW_MAJOR, MKL_NOTRANS, MKL_NOTRANS, nVar, nVar, nVar,  1.0, nVar, nVar, 0.0, nVar );
  MatrixMatrixProductKernel = mkl_jit_get_dgemm_ptr( MatrixMatrixProductJitter );

  mkl_jit_create_dgemm( &MatrixVectorProductJitterBetaZero, MKL_COL_MAJOR, MKL_NOTRANS, MKL_NOTRANS, 1, nVar, nVar,  1.0, 1, nVar, 0.0, 1 );
  MatrixVectorProductKernelBetaZero = mkl_jit_get_dgemm_ptr( MatrixVectorProductJitterBetaZero );

  mkl_jit_create_dgemm( &MatrixVectorProductJitterBetaOne, MKL_COL_MAJOR, MKL_NOTRANS, MKL_NOTRANS, 1, nVar, nVar,  1.0, 1, nVar, 1.0, 1 );
  MatrixVectorProductKernelBetaOne = mkl_jit_get_dgemm_ptr( MatrixVectorProductJitterBetaOne );

  mkl_jit_create_dgemm( &MatrixVectorProductJitterAlphaMinusOne, MKL_COL_MAJOR, MKL_NOTRANS, MKL_NOTRANS, 1, nVar, nVar, -1.0, 1, nVar, 1.0, 1 );
  MatrixVectorProductKernelAlphaMinusOne = mkl_jit_get_dgemm_ptr( MatrixVectorProductJitterAlphaMinusOne );

  mkl_jit_create_dgemm( &MatrixVectorProductTranspJitterBetaOne, MKL_COL_MAJOR, MKL_NOTRANS, MKL_NOTRANS, nVar, 1, nVar,  1.0, nVar, nVar, 1.0, nVar );
  MatrixVectorProductTranspKernelBetaOne = mkl_jit_get_dgemm_ptr( MatrixVectorProductTranspJitterBetaOne );

  mkl_ipiv = new lapack_int [ nVar ];
#endif

}

template<class ScalarType>
template<class OtherType>
void CSysMatrix<ScalarType>::InitiateComms(CSysVector<OtherType> & x,
                                           CGeometry *geometry,
                                           CConfig *config,
                                           unsigned short commType) {

  /*--- Local variables ---*/

  unsigned short iVar;
  unsigned short COUNT_PER_POINT = 0;
  unsigned short MPI_TYPE        = 0;

  unsigned long iPoint, msg_offset, buf_offset;

  int iMessage, iSend, nSend;

  /*--- Create a boolean for reversing the order of comms. ---*/

  bool reverse = false;

  /*--- Set the size of the data packet and type depending on quantity. ---*/

  switch (commType) {
    case SOLUTION_MATRIX:
      COUNT_PER_POINT  = nVar;
      MPI_TYPE         = COMM_TYPE_DOUBLE;
      reverse          = false;
      break;
    case SOLUTION_MATRIXTRANS:
      COUNT_PER_POINT  = nVar;
      MPI_TYPE         = COMM_TYPE_DOUBLE;
      reverse          = true;
      break;
    default:
      SU2_MPI::Error("Unrecognized quantity for point-to-point MPI comms.",
                     CURRENT_FUNCTION);
      break;
  }

  /*--- Check to make sure we have created a large enough buffer
   for these comms during preprocessing. This is only for the su2double
   buffer. It will be reallocated whenever we find a larger count
   per point. After the first cycle of comms, this should be inactive. ---*/

  if (COUNT_PER_POINT > geometry->countPerPoint) {
    geometry->AllocateP2PComms(COUNT_PER_POINT);
  }

  /*--- Set some local pointers to make access simpler. ---*/

  su2double *bufDSend = geometry->bufD_P2PSend;

  /*--- Load the specified quantity from the solver into the generic
   communication buffer in the geometry class. ---*/

  if (geometry->nP2PSend > 0) {

    /*--- Post all non-blocking recvs first before sends. ---*/

    geometry->PostP2PRecvs(geometry, config, MPI_TYPE, reverse);

    for (iMessage = 0; iMessage < geometry->nP2PSend; iMessage++) {

      switch (commType) {

        case SOLUTION_MATRIX:

          /*--- Get the offset for the start of this message. ---*/

          msg_offset = geometry->nPoint_P2PSend[iMessage];

          /*--- Total count can include multiple pieces of data per point. ---*/

          nSend = (geometry->nPoint_P2PSend[iMessage+1] -
                   geometry->nPoint_P2PSend[iMessage]);

          for (iSend = 0; iSend < nSend; iSend++) {

            /*--- Get the local index for this communicated data. ---*/

            iPoint = geometry->Local_Point_P2PSend[msg_offset + iSend];

            /*--- Compute the offset in the recv buffer for this point. ---*/

            buf_offset = (msg_offset + iSend)*geometry->countPerPoint;

            /*--- Load the buffer with the data to be sent. ---*/

            for (iVar = 0; iVar < nVar; iVar++)
              bufDSend[buf_offset+iVar] = x[iPoint*nVar+iVar];

          }

          break;

        case SOLUTION_MATRIXTRANS:

          /*--- We are going to communicate in reverse, so we use the
           recv buffer for the send instead. Also, all of the offsets
           and counts are derived from the recv data structures. ---*/

          bufDSend = geometry->bufD_P2PRecv;

          /*--- Get the offset for the start of this message. ---*/

          msg_offset = geometry->nPoint_P2PRecv[iMessage];

          /*--- Total count can include multiple pieces of data per point. ---*/

          nSend = (geometry->nPoint_P2PRecv[iMessage+1] -
                   geometry->nPoint_P2PRecv[iMessage]);

          for (iSend = 0; iSend < nSend; iSend++) {

            /*--- Get the local index for this communicated data. Here we
             again use the recv structure to find the send point, since
             the usual recv points are now the senders in reverse mode. ---*/

            iPoint = geometry->Local_Point_P2PRecv[msg_offset + iSend];

            /*--- Compute the offset in the recv buffer for this point. ---*/

            buf_offset = (msg_offset + iSend)*geometry->countPerPoint;

            /*--- Load the buffer with the data to be sent. ---*/

            for (iVar = 0; iVar < nVar; iVar++)
              bufDSend[buf_offset+iVar] = x[iPoint*nVar+iVar];

          }

          break;

        default:
          SU2_MPI::Error("Unrecognized quantity for point-to-point MPI comms.",
                         CURRENT_FUNCTION);
          break;

      }

      /*--- Launch the point-to-point MPI send for this message. ---*/

      geometry->PostP2PSends(geometry, config, MPI_TYPE, iMessage, reverse);

    }
  }

}

template<class ScalarType>
template<class OtherType>
void CSysMatrix<ScalarType>::CompleteComms(CSysVector<OtherType> & x,
                                           CGeometry *geometry,
                                           CConfig *config,
                                           unsigned short commType) {

  /*--- Local variables ---*/

  unsigned short iVar;
  unsigned long iPoint, iRecv, nRecv, msg_offset, buf_offset;

  int ind, source, iMessage, jRecv;
  SU2_MPI::Status status;

  /*--- Set some local pointers to make access simpler. ---*/

  su2double *bufDRecv = geometry->bufD_P2PRecv;

  /*--- Store the data that was communicated into the appropriate
   location within the local class data structures. ---*/

  if (geometry->nP2PRecv > 0) {

    for (iMessage = 0; iMessage < geometry->nP2PRecv; iMessage++) {

      /*--- For efficiency, recv the messages dynamically based on
       the order they arrive. ---*/

      SU2_MPI::Waitany(geometry->nP2PRecv, geometry->req_P2PRecv,
                       &ind, &status);

      /*--- Once we have recv'd a message, get the source rank. ---*/

      source = status.MPI_SOURCE;

      switch (commType) {
        case SOLUTION_MATRIX:

          /*--- We know the offsets based on the source rank. ---*/

          jRecv = geometry->P2PRecv2Neighbor[source];

          /*--- Get the offset for the start of this message. ---*/

          msg_offset = geometry->nPoint_P2PRecv[jRecv];

          /*--- Get the number of packets to be received in this message. ---*/

          nRecv = (geometry->nPoint_P2PRecv[jRecv+1] -
                   geometry->nPoint_P2PRecv[jRecv]);

          for (iRecv = 0; iRecv < nRecv; iRecv++) {

            /*--- Get the local index for this communicated data. ---*/

            iPoint = geometry->Local_Point_P2PRecv[msg_offset + iRecv];

            /*--- Compute the offset in the recv buffer for this point. ---*/

            buf_offset = (msg_offset + iRecv)*geometry->countPerPoint;

            /*--- Store the data correctly depending on the quantity. ---*/

            for (iVar = 0; iVar < nVar; iVar++)
              x[iPoint*nVar+iVar] = ActiveAssign<OtherType,su2double>(bufDRecv[buf_offset+iVar]);

          }
          break;

        case SOLUTION_MATRIXTRANS:

          /*--- We are going to communicate in reverse, so we use the
           send buffer for the recv instead. Also, all of the offsets
           and counts are derived from the send data structures. ---*/

          bufDRecv = geometry->bufD_P2PSend;

          /*--- We know the offsets based on the source rank. ---*/

          jRecv = geometry->P2PSend2Neighbor[source];

          /*--- Get the offset for the start of this message. ---*/

          msg_offset = geometry->nPoint_P2PSend[jRecv];

          /*--- Get the number of packets to be received in this message. ---*/

          nRecv = (geometry->nPoint_P2PSend[jRecv+1] -
                   geometry->nPoint_P2PSend[jRecv]);

          for (iRecv = 0; iRecv < nRecv; iRecv++) {

            /*--- Get the local index for this communicated data. ---*/

            iPoint = geometry->Local_Point_P2PSend[msg_offset + iRecv];

            /*--- Compute the offset in the recv buffer for this point. ---*/

            buf_offset = (msg_offset + iRecv)*geometry->countPerPoint;


            for (iVar = 0; iVar < nVar; iVar++)
              x[iPoint*nVar+iVar] += ActiveAssign<OtherType,su2double>(bufDRecv[buf_offset+iVar]);

          }

          break;
        default:
          SU2_MPI::Error("Unrecognized quantity for point-to-point MPI comms.",
                         CURRENT_FUNCTION);
          break;
      }
    }

    /*--- Verify that all non-blocking point-to-point sends have finished.
     Note that this should be satisfied, as we have received all of the
     data in the loop above at this point. ---*/

#ifdef HAVE_MPI
    SU2_MPI::Waitall(geometry->nP2PSend, geometry->req_P2PSend, MPI_STATUS_IGNORE);
#endif

  }

}

template<class ScalarType>
void CSysMatrix<ScalarType>::Gauss_Elimination(ScalarType* matrix, ScalarType* vec) {

#ifdef USE_MKL_LAPACK
  // With MKL_DIRECT_CALL enabled, this is significantly faster than native code on Intel Architectures.
  LAPACKE_dgetrf( LAPACK_ROW_MAJOR, nVar, nVar, matrix, nVar, mkl_ipiv );
  LAPACKE_dgetrs( LAPACK_ROW_MAJOR, 'N', nVar, 1, matrix, nVar, mkl_ipiv, vec, 1 );
#else
  int iVar, jVar, kVar, nvar = int(nVar);
  ScalarType weight;

  /*--- Transform system in Upper Matrix ---*/
  for (iVar = 1; iVar < nvar; iVar++) {
    for (jVar = 0; jVar < iVar; jVar++) {
      weight = matrix[iVar*nvar+jVar] / matrix[jVar*nvar+jVar];
      for (kVar = jVar; kVar < nvar; kVar++)
        matrix[iVar*nvar+kVar] -= weight*matrix[jVar*nvar+kVar];
      vec[iVar] -= weight*vec[jVar];
    }
  }

  /*--- Backwards substitution ---*/
  for (iVar = nvar-1; iVar >= 0; iVar--) {
    for (jVar = iVar+1; jVar < nvar; jVar++)
      vec[iVar] -= matrix[iVar*nvar+jVar]*vec[jVar];
    vec[iVar] /= matrix[iVar*nvar+iVar];
  }
#endif
}

template<class ScalarType>
void CSysMatrix<ScalarType>::MatrixInverse(const ScalarType *matrix, ScalarType *inverse) {

  /*---
   This is a generalization of Gaussian elimination for multiple rhs' (the basis vectors).
   We could call "Gauss_Elimination" multiple times or fully generalize it for multiple rhs,
   the performance of both routines would suffer in both cases without the use of exotic templating.
   And so it feels reasonable to have some duplication here.
  ---*/

  int iVar, jVar, nvar = int(nVar);

  /*--- Initialize the inverse and make a copy of the matrix ---*/
  for (iVar = 0; iVar < nvar; iVar++) {
    for (jVar = 0; jVar < nvar; jVar++) {
      block[iVar*nvar+jVar] = matrix[iVar*nvar+jVar];
      inverse[iVar*nvar+jVar] = ScalarType(iVar==jVar); // identity
    }
  }

  /*--- Inversion ---*/
#ifdef USE_MKL_LAPACK
  // With MKL_DIRECT_CALL enabled, this is significantly faster than native code on Intel Architectures.
  LAPACKE_dgetrf( LAPACK_ROW_MAJOR, nVar, nVar, block, nVar, mkl_ipiv );
  LAPACKE_dgetrs( LAPACK_ROW_MAJOR, 'N', nVar, nVar, block, nVar, mkl_ipiv, inverse, nVar );
#else
  int kVar;
  ScalarType weight;

  /*--- Transform system in Upper Matrix ---*/
  for (iVar = 1; iVar < nvar; iVar++) {
    for (jVar = 0; jVar < iVar; jVar++)
    {
      weight = block[iVar*nvar+jVar] / block[jVar*nvar+jVar];

      for (kVar = jVar; kVar < nvar; kVar++)
        block[iVar*nvar+kVar] -= weight*block[jVar*nvar+kVar];

      /*--- at this stage "inverse" is lower triangular so not all cols need updating ---*/
      for (kVar = 0; kVar <= jVar; kVar++)
        inverse[iVar*nvar+kVar] -= weight*inverse[jVar*nvar+kVar];
    }
  }

  /*--- Backwards substitution ---*/
  for (iVar = nvar-1; iVar >= 0; iVar--)
  {
    for (jVar = iVar+1; jVar < nvar; jVar++)
      for (kVar = 0; kVar < nvar; kVar++)
        inverse[iVar*nvar+kVar] -= block[iVar*nvar+jVar] * inverse[jVar*nvar+kVar];

    for (kVar = 0; kVar < nvar; kVar++)
      inverse[iVar*nvar+kVar] /= block[iVar*nvar+iVar];
  }
#endif
}

template<class ScalarType>
void CSysMatrix<ScalarType>::DeleteValsRowi(unsigned long i) {

  unsigned long block_i = i/nVar;
  unsigned long row = i - block_i*nVar;
  unsigned long index, iVar;

  for (index = row_ptr[block_i]; index < row_ptr[block_i+1]; index++) {
    for (iVar = 0; iVar < nVar; iVar++)
      matrix[index*nVar*nVar+row*nVar+iVar] = 0.0; // Delete row values in the block
    if (col_ind[index] == block_i)
      matrix[index*nVar*nVar+row*nVar+row] = 1.0; // Set 1 to the diagonal element
  }

}

template<class ScalarType>
void CSysMatrix<ScalarType>::RowProduct(const CSysVector<ScalarType> & vec, unsigned long row_i) {

  unsigned long iVar, index, col_j;

  for (iVar = 0; iVar < nVar; iVar++)
    prod_row_vector[iVar] = 0;

  for (index = row_ptr[row_i]; index < row_ptr[row_i+1]; index++) {
    col_j = col_ind[index];
    MatrixVectorProductAdd(&matrix[index*nVar*nVar], &vec[col_j*nVar], prod_row_vector);
  }

}

template<class ScalarType>
void CSysMatrix<ScalarType>::MatrixVectorProduct(const CSysVector<ScalarType> & vec, CSysVector<ScalarType> & prod, CGeometry *geometry, CConfig *config) {

  unsigned long prod_begin, vec_begin, mat_begin, index, row_i;

  /*--- Some checks for consistency between CSysMatrix and the CSysVector<ScalarType>s ---*/
#ifndef NDEBUG
  if ( (nVar != vec.GetNVar()) || (nVar != prod.GetNVar()) ) {
    SU2_MPI::Error("nVar values incompatible.", CURRENT_FUNCTION);
  }
  if ( (nPoint != vec.GetNBlk()) || (nPoint != prod.GetNBlk()) ) {
    SU2_MPI::Error("nPoint and nBlk values incompatible.", CURRENT_FUNCTION);
  }
#endif

  prod = ScalarType(0.0); // set all entries of prod to zero
  for (row_i = 0; row_i < nPointDomain; row_i++) {
    prod_begin = row_i*nVar; // offset to beginning of block row_i
    for (index = row_ptr[row_i]; index < row_ptr[row_i+1]; index++) {
      vec_begin = col_ind[index]*nVar; // offset to beginning of block col_ind[index]
      mat_begin = (index*nVar*nVar); // offset to beginning of matrix block[row_i][col_ind[indx]]
      MatrixVectorProductAdd(&matrix[mat_begin], &vec[vec_begin], &prod[prod_begin]);
    }
  }

  /*--- MPI Parallelization ---*/

  InitiateComms(prod, geometry, config, SOLUTION_MATRIX);
  CompleteComms(prod, geometry, config, SOLUTION_MATRIX);

}

template<class ScalarType>
void CSysMatrix<ScalarType>::MatrixVectorProductTransposed(const CSysVector<ScalarType> & vec, CSysVector<ScalarType> & prod, CGeometry *geometry, CConfig *config) {

  unsigned long prod_begin, vec_begin, mat_begin, index, row_i;

  /*--- Some checks for consistency between CSysMatrix and the CSysVector<ScalarType>s ---*/
#ifndef NDEBUG
  if ( (nVar != vec.GetNVar()) || (nVar != prod.GetNVar()) ) {
    SU2_MPI::Error("nVar values incompatible.", CURRENT_FUNCTION);
  }
  if ( (nPoint != vec.GetNBlk()) || (nPoint != prod.GetNBlk()) ) {
    SU2_MPI::Error("nPoint and nBlk values incompatible.", CURRENT_FUNCTION);
  }
#endif

  prod = ScalarType(0.0); // set all entries of prod to zero
  for (row_i = 0; row_i < nPointDomain; row_i++) {
    vec_begin = row_i*nVar; // offset to beginning of block col_ind[index]
    for (index = row_ptr[row_i]; index < row_ptr[row_i+1]; index++) {
      prod_begin = col_ind[index]*nVar; // offset to beginning of block row_i
      mat_begin = (index*nVar*nVar); // offset to beginning of matrix block[row_i][col_ind[indx]]
      MatrixVectorProductTransp(&matrix[mat_begin], &vec[vec_begin], &prod[prod_begin]);
    }
  }

  /*--- MPI Parallelization ---*/

  InitiateComms(prod, geometry, config, SOLUTION_MATRIXTRANS);
  CompleteComms(prod, geometry, config, SOLUTION_MATRIXTRANS);

}

template<class ScalarType>
void CSysMatrix<ScalarType>::BuildJacobiPreconditioner(bool transpose) {

  /*--- Build Jacobi preconditioner (M = D), compute and store the inverses of the diagonal blocks. ---*/
  for (unsigned long iPoint = 0; iPoint < nPointDomain; iPoint++)
    InverseDiagonalBlock(iPoint, &(invM[iPoint*nVar*nVar]), transpose);

}

template<class ScalarType>
void CSysMatrix<ScalarType>::ComputeJacobiPreconditioner(const CSysVector<ScalarType> & vec, CSysVector<ScalarType> & prod, CGeometry *geometry, CConfig *config) {

  /*--- Apply Jacobi preconditioner, y = D^{-1} * x, the inverse of the diagonal is already known. ---*/
  for (unsigned long iPoint = 0; iPoint < nPointDomain; iPoint++)
    MatrixVectorProduct(&(invM[iPoint*nVar*nVar]), &vec[iPoint*nVar], &prod[iPoint*nVar]);

  /*--- MPI Parallelization ---*/
  InitiateComms(prod, geometry, config, SOLUTION_MATRIX);
  CompleteComms(prod, geometry, config, SOLUTION_MATRIX);

}

template<class ScalarType>
void CSysMatrix<ScalarType>::BuildILUPreconditioner(bool transposed) {

  unsigned long index, index_, iVar;
  ScalarType *Block_ij, *Block_ik;
  const ScalarType *Block_jk;
  long iPoint, jPoint, kPoint;

  /*--- Copy block matrix to compute factorization in-place. ---*/

  if ((ilu_fill_in == 0) && !transposed) {
    /*--- ILU0, direct copy. ---*/
    for (iVar = 0; iVar < nnz*nVar*nVar; ++iVar)
      ILU_matrix[iVar] = matrix[iVar];
  }
  else {
    /*--- ILUn clear the ILU matrix first, for ILU0^T
     *    the copy takes care of the clearing. ---*/
    if (ilu_fill_in > 0)
      for (iVar = 0; iVar < nnz_ilu*nVar*nVar; iVar++)
        ILU_matrix[iVar] = 0.0;

    /*--- Transposed or ILUn, traverse matrix to access its blocks
     *    sequentially and set them in the ILU matrix. ---*/
    for (iPoint = 0; iPoint < (long)nPointDomain; iPoint++) {
      for (index = row_ptr[iPoint]; index < row_ptr[iPoint+1]; index++) {
        jPoint = col_ind[index];
        Block_ij = &matrix[index*nVar*nVar];
        if (transposed) {
          SetBlockTransposed_ILUMatrix(jPoint, iPoint, Block_ij);
        } else {
          SetBlock_ILUMatrix(iPoint, jPoint, Block_ij);
        }
      }
    }
  }

  /*--- Transform system in Upper Matrix ---*/

  for (iPoint = 1; iPoint < (long)nPointDomain; iPoint++) {

    /*--- Invert and store the previous diagonal block to later compute the weight. ---*/

    InverseDiagonalBlock_ILUMatrix(iPoint-1, &invM[(iPoint-1)*nVar*nVar]);

    /*--- For this row (unknown), loop over its lower diagonal entries. ---*/

    for (index = row_ptr_ilu[iPoint]; index < dia_ptr_ilu[iPoint]; index++) {

      /*--- jPoint is the column index (jPoint < iPoint). ---*/

      jPoint = col_ind_ilu[index];

      /*--- Multiply the block by the inverse of the corresponding diagonal block. ---*/

      Block_ij = &ILU_matrix[index*nVar*nEqn];
      MatrixMatrixProduct(Block_ij, &invM[jPoint*nVar*nVar], block_weight);

      /*--- block_weight holds Aij*inv(Ajj). Jump to the upper part of the jPoint row. ---*/

      for (index_ = dia_ptr_ilu[jPoint]+1; index_ < row_ptr_ilu[jPoint+1]; index_++) {

        /*--- Get the column index (kPoint > jPoint). ---*/

        kPoint = col_ind_ilu[index_];

        /*--- If Aik exists, update it: Aik -= Aij*inv(Ajj)*Ajk ---*/

        Block_ik = GetBlock_ILUMatrix(iPoint, kPoint);

        if (Block_ik != nullptr) {
          Block_jk = &ILU_matrix[index_*nVar*nEqn];
          MatrixMatrixProduct(block_weight, Block_jk, block);
          MatrixSubtraction(Block_ik, block, Block_ik);
        }
      }

      /*--- Lastly, store block_weight in the lower triangular part, which
       will be reused during the forward solve in the precon/smoother. ---*/

      for (iVar = 0; iVar < nVar*nEqn; ++iVar)
        Block_ij[iVar] = block_weight[iVar];
    }
  }

  InverseDiagonalBlock_ILUMatrix(nPointDomain-1, &invM[(nPointDomain-1)*nVar*nVar]);

}

template<class ScalarType>
void CSysMatrix<ScalarType>::ComputeILUPreconditioner(const CSysVector<ScalarType> & vec, CSysVector<ScalarType> & prod, CGeometry *geometry, CConfig *config) {

  unsigned long index, iVar;
  const ScalarType *Block_ij;
  long iPoint, jPoint;

  /*--- Copy vector to then work on prod in place ---*/

  for (iPoint = 0; iPoint < long(nPointDomain*nVar); iPoint++)
    prod[iPoint] = vec[iPoint];

  /*--- Forward solve the system using the lower matrix entries that
   were computed and stored during the ILU preprocessing. Note
   that we are overwriting the residual vector as we go. ---*/

  for (iPoint = 1; iPoint < (long)nPointDomain; iPoint++) {
    for (index = row_ptr_ilu[iPoint]; index < dia_ptr_ilu[iPoint]; index++) {
      jPoint = col_ind_ilu[index];
      Block_ij = &ILU_matrix[index*nVar*nEqn];
      MatrixVectorProductSub(Block_ij, &prod[jPoint*nVar], &prod[iPoint*nVar]);
    }
  }

  /*--- Backwards substitution (starts at the last row) ---*/

  for (iPoint = nPointDomain-1; iPoint >= 0; iPoint--) {

    for (iVar = 0; iVar < nVar; iVar++)
      sum_vector[iVar] = prod[iPoint*nVar+iVar];

    for (index = dia_ptr_ilu[iPoint]+1; index < row_ptr_ilu[iPoint+1]; index++) {
      jPoint = col_ind_ilu[index];
      if (jPoint < (long)nPointDomain) {
        Block_ij = &ILU_matrix[index*nVar*nEqn];
        MatrixVectorProductSub(Block_ij, &prod[jPoint*nVar], sum_vector);
      }
    }

    MatrixVectorProduct(&invM[iPoint*nVar*nVar], sum_vector, &prod[iPoint*nVar]);
  }

  /*--- MPI Parallelization ---*/

  InitiateComms(prod, geometry, config, SOLUTION_MATRIX);
  CompleteComms(prod, geometry, config, SOLUTION_MATRIX);

}

template<class ScalarType>
void CSysMatrix<ScalarType>::ComputeLU_SGSPreconditioner(const CSysVector<ScalarType> & vec, CSysVector<ScalarType> & prod, CGeometry *geometry, CConfig *config) {
  unsigned long iPoint, iVar;

  /*--- First part of the symmetric iteration: (D+L).x* = b ---*/

  for (iPoint = 0; iPoint < nPointDomain; iPoint++) {
    LowerProduct(prod, iPoint);                                               // Compute L.x*
    for (iVar = 0; iVar < nVar; iVar++)
      prod[iPoint*nVar+iVar] = vec[iPoint*nVar+iVar] - prod_row_vector[iVar]; // Compute aux_vector = b - L.x*
    Gauss_Elimination(iPoint, &prod[iPoint*nVar]);                            // Solve D.x* = aux_vector
  }

  /*--- MPI Parallelization ---*/

  InitiateComms(prod, geometry, config, SOLUTION_MATRIX);
  CompleteComms(prod, geometry, config, SOLUTION_MATRIX);

  /*--- Second part of the symmetric iteration: (D+U).x_(1) = D.x* ---*/

  for (iPoint = nPointDomain-1; (int)iPoint >= 0; iPoint--) {
    DiagonalProduct(prod, iPoint);                                        // Compute D.x*
    for (iVar = 0; iVar < nVar; iVar++)
      aux_vector[iVar] = prod_row_vector[iVar];                           // Compute aux_vector = D.x*
    UpperProduct(prod, iPoint);                                           // Compute U.x_(n+1)
    for (iVar = 0; iVar < nVar; iVar++)
      prod[iPoint*nVar+iVar] = aux_vector[iVar] - prod_row_vector[iVar];  // Compute aux_vector = D.x*-U.x_(n+1)
    Gauss_Elimination(iPoint, &prod[iPoint*nVar]);                        // Solve D.x* = aux_vector
  }

  /*--- MPI Parallelization ---*/

  InitiateComms(prod, geometry, config, SOLUTION_MATRIX);
  CompleteComms(prod, geometry, config, SOLUTION_MATRIX);

}

template<class ScalarType>
unsigned short CSysMatrix<ScalarType>::BuildLineletPreconditioner(CGeometry *geometry, CConfig *config) {

  bool add_point;
  unsigned long iEdge, iPoint, jPoint, index_Point, iLinelet, iVertex, next_Point, counter, iElem;
  unsigned short iMarker, iNode, MeanPoints;
  su2double alpha = 0.9, weight, max_weight, *normal, area, volume_iPoint, volume_jPoint;
  unsigned long Local_nPoints, Local_nLineLets, Global_nPoints, Global_nLineLets, max_nElem;

  /*--- Memory allocation --*/

  vector<bool> check_Point(nPoint,true);

  LineletBool.resize(nPoint,false);

  nLinelet = 0;
  for (iMarker = 0; iMarker < config->GetnMarker_All(); iMarker++) {
    if ((config->GetMarker_All_KindBC(iMarker) == HEAT_FLUX              ) ||
        (config->GetMarker_All_KindBC(iMarker) == ISOTHERMAL             ) ||
        (config->GetMarker_All_KindBC(iMarker) == EULER_WALL             ) ||
        (config->GetMarker_All_KindBC(iMarker) == DISPLACEMENT_BOUNDARY)) {
      nLinelet += geometry->nVertex[iMarker];
    }
  }

  /*--- If the domain contains well defined Linelets ---*/

  if (nLinelet != 0) {

    /*--- Basic initial allocation ---*/

    LineletPoint.resize(nLinelet);

    /*--- Define the basic linelets, starting from each vertex ---*/

    iLinelet = 0;

    for (iMarker = 0; iMarker < config->GetnMarker_All(); iMarker++) {
      if ((config->GetMarker_All_KindBC(iMarker) == HEAT_FLUX              ) ||
          (config->GetMarker_All_KindBC(iMarker) == ISOTHERMAL             ) ||
          (config->GetMarker_All_KindBC(iMarker) == EULER_WALL             ) ||
          (config->GetMarker_All_KindBC(iMarker) == DISPLACEMENT_BOUNDARY))
      {
        for (iVertex = 0; iVertex < geometry->nVertex[iMarker]; iVertex++) {
          iPoint = geometry->vertex[iMarker][iVertex]->GetNode();
          LineletPoint[iLinelet].push_back(iPoint);
          check_Point[iPoint] = false;
          iLinelet++;
        }
      }
    }

    /*--- Create the linelet structure ---*/

    iLinelet = 0;

    do {

      index_Point = 0;

      do {

        /*--- Compute the value of the max weight ---*/

        iPoint = LineletPoint[iLinelet][index_Point];
        max_weight = 0.0;
        for (iNode = 0; iNode < geometry->node[iPoint]->GetnPoint(); iNode++) {
          jPoint = geometry->node[iPoint]->GetPoint(iNode);
          if ((check_Point[jPoint]) && geometry->node[jPoint]->GetDomain()) {
            iEdge = geometry->FindEdge(iPoint, jPoint);
            normal = geometry->edge[iEdge]->GetNormal();
            if (geometry->GetnDim() == 3) area = sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
            else area = sqrt(normal[0]*normal[0]+normal[1]*normal[1]);
            volume_iPoint = geometry->node[iPoint]->GetVolume();
            volume_jPoint = geometry->node[jPoint]->GetVolume();
            weight = 0.5*area*((1.0/volume_iPoint)+(1.0/volume_jPoint));
            max_weight = max(max_weight, weight);
          }
        }

        /*--- Verify if any face of the control volume must be added ---*/

        add_point = false;
        counter = 0;
        next_Point = geometry->node[iPoint]->GetPoint(0);
        for (iNode = 0; iNode < geometry->node[iPoint]->GetnPoint(); iNode++) {
          jPoint = geometry->node[iPoint]->GetPoint(iNode);
          iEdge = geometry->FindEdge(iPoint, jPoint);
          normal = geometry->edge[iEdge]->GetNormal();
          if (geometry->GetnDim() == 3) area = sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
          else area = sqrt(normal[0]*normal[0]+normal[1]*normal[1]);
          volume_iPoint = geometry->node[iPoint]->GetVolume();
          volume_jPoint = geometry->node[jPoint]->GetVolume();
          weight = 0.5*area*((1.0/volume_iPoint)+(1.0/volume_jPoint));
          if (((check_Point[jPoint]) && (weight/max_weight > alpha) && (geometry->node[jPoint]->GetDomain())) &&
              ((index_Point == 0) || ((index_Point > 0) && (jPoint != LineletPoint[iLinelet][index_Point-1])))) {
            add_point = true;
            next_Point = jPoint;
            counter++;
          }
        }

        /*--- We have arrived to an isotropic zone ---*/

        if (counter > 1) add_point = false;

        /*--- Add a typical point to the linelet, no leading edge ---*/

        if (add_point) {
          LineletPoint[iLinelet].push_back(next_Point);
          check_Point[next_Point] = false;
          index_Point++;
        }

      } while (add_point);
      iLinelet++;
    } while (iLinelet < nLinelet);

    /*--- Identify the points that belong to a Linelet ---*/

    for (iLinelet = 0; iLinelet < nLinelet; iLinelet++) {
      for (iElem = 0; iElem < LineletPoint[iLinelet].size(); iElem++) {
        iPoint = LineletPoint[iLinelet][iElem];
        LineletBool[iPoint] = true;
      }
    }

    /*--- Identify the maximum number of elements in a Linelet ---*/

    max_nElem = LineletPoint[0].size();
    for (iLinelet = 1; iLinelet < nLinelet; iLinelet++)
      if (LineletPoint[iLinelet].size() > max_nElem)
        max_nElem = LineletPoint[iLinelet].size();

  }

  /*--- The domain doesn't have well defined linelets ---*/

  else {

    max_nElem = 0;

  }

  /*--- Screen output ---*/

  Local_nPoints = 0;
  for (iLinelet = 0; iLinelet < nLinelet; iLinelet++) {
    Local_nPoints += LineletPoint[iLinelet].size();
  }
  Local_nLineLets = nLinelet;

  SU2_MPI::Allreduce(&Local_nPoints, &Global_nPoints, 1, MPI_UNSIGNED_LONG, MPI_SUM, MPI_COMM_WORLD);
  SU2_MPI::Allreduce(&Local_nLineLets, &Global_nLineLets, 1, MPI_UNSIGNED_LONG, MPI_SUM, MPI_COMM_WORLD);

  MeanPoints = SU2_TYPE::Int(ScalarType(Global_nPoints)/ScalarType(Global_nLineLets));

  /*--- Memory allocation --*/

  LineletUpper.resize(max_nElem,nullptr);
  LineletInvDiag.resize(max_nElem*nVar*nVar,0.0);
  LineletVector.resize(max_nElem*nVar,0.0);

  return MeanPoints;

}

template<class ScalarType>
void CSysMatrix<ScalarType>::ComputeLineletPreconditioner(const CSysVector<ScalarType> & vec, CSysVector<ScalarType> & prod,
                                              CGeometry *geometry, CConfig *config) {

  unsigned long iVar, iElem, nElem, iLinelet, iPoint, im1Point;
  /*--- Pointers to lower, upper, and diagonal blocks ---*/
  const ScalarType *l = nullptr, *u = nullptr, *d = nullptr;
  /*--- Inverse of d_{i-1}, modified d_i, modified b_i (rhs) ---*/
  ScalarType *inv_dm1 = nullptr, *d_prime = nullptr, *b_prime = nullptr;

  /*--- Jacobi preconditioning where there is no linelet ---*/

  for (iPoint = 0; iPoint < nPointDomain; iPoint++)
    if (!LineletBool[iPoint])
      MatrixVectorProduct(&(invM[iPoint*nVar*nVar]), &vec[iPoint*nVar], &prod[iPoint*nVar]);

  /*--- MPI Parallelization ---*/

  InitiateComms(prod, geometry, config, SOLUTION_MATRIX);
  CompleteComms(prod, geometry, config, SOLUTION_MATRIX);

  /*--- Solve linelet using the Thomas algorithm ---*/

  for (iLinelet = 0; iLinelet < nLinelet; iLinelet++) {

    nElem = LineletPoint[iLinelet].size();

    /*--- Initialize the solution vector with the rhs ---*/

    for (iElem = 0; iElem < nElem; iElem++) {
      iPoint = LineletPoint[iLinelet][iElem];
      for (iVar = 0; iVar < nVar; iVar++)
        LineletVector[iElem*nVar+iVar] = vec[iPoint*nVar+iVar];
    }

    /*--- Forward pass, eliminate lower entries, modify diagonal and rhs ---*/

    iPoint = LineletPoint[iLinelet][0];
    d = GetBlock(iPoint, iPoint);
    for (iVar = 0; iVar < nVar*nVar; ++iVar)
      LineletInvDiag[iVar] = d[iVar];

    for (iElem = 1; iElem < nElem; iElem++) {

      /*--- Setup pointers to required matrices and vectors ---*/
      im1Point = LineletPoint[iLinelet][iElem-1];
      iPoint = LineletPoint[iLinelet][iElem];

      d = GetBlock(iPoint, iPoint);
      l = GetBlock(iPoint, im1Point);
      u = GetBlock(im1Point, iPoint);

      inv_dm1 = &LineletInvDiag[(iElem-1)*nVar*nVar];
      d_prime = &LineletInvDiag[iElem*nVar*nVar];
      b_prime = &LineletVector[iElem*nVar];

      /*--- Invert previous modified diagonal ---*/
      MatrixInverse(inv_dm1, inv_dm1);

      /*--- Left-multiply by lower block to obtain the weight ---*/
      MatrixMatrixProduct(l, inv_dm1, block_weight);

      /*--- Multiply weight by upper block to modify current diagonal ---*/
      MatrixMatrixProduct(block_weight, u, d_prime);
      MatrixSubtraction(d, d_prime, d_prime);

      /*--- Update the rhs ---*/
      MatrixVectorProduct(block_weight, &LineletVector[(iElem-1)*nVar], aux_vector);
      VectorSubtraction(b_prime, aux_vector, b_prime);

      /*--- Cache upper block pointer for the backward substitution phase ---*/
      LineletUpper[iElem-1] = u;
    }

    /*--- Backwards substitution, LineletVector becomes the solution ---*/

    /*--- x_n = d_n^{-1} * b_n ---*/
    Gauss_Elimination(&LineletInvDiag[(nElem-1)*nVar*nVar], &LineletVector[(nElem-1)*nVar]);

    /*--- x_i = d_i^{-1}*(b_i - u_i*x_{i+1}) ---*/
    for (iElem = nElem-1; iElem > 0; --iElem) {
      inv_dm1 = &LineletInvDiag[(iElem-1)*nVar*nVar];
      MatrixVectorProduct(LineletUpper[iElem-1], &LineletVector[iElem*nVar], aux_vector);
      VectorSubtraction(&LineletVector[(iElem-1)*nVar], aux_vector, aux_vector);
      MatrixVectorProduct(inv_dm1, aux_vector, &LineletVector[(iElem-1)*nVar]);
    }

    /*--- Copy results to product vector ---*/

    for (iElem = 0; iElem < nElem; iElem++) {
      iPoint = LineletPoint[iLinelet][iElem];
      for (iVar = 0; iVar < nVar; iVar++)
        prod[iPoint*nVar+iVar] = LineletVector[iElem*nVar+iVar];
    }

  }

  /*--- MPI Parallelization ---*/

  InitiateComms(prod, geometry, config, SOLUTION_MATRIX);
  CompleteComms(prod, geometry, config, SOLUTION_MATRIX);

}

template<class ScalarType>
void CSysMatrix<ScalarType>::ComputeResidual(const CSysVector<ScalarType> & sol, const CSysVector<ScalarType> & f, CSysVector<ScalarType> & res) {

  for (unsigned long iPoint = 0; iPoint < nPointDomain; iPoint++) {
    RowProduct(sol, iPoint);
    VectorSubtraction(prod_row_vector, &f[iPoint*nVar], &res[iPoint*nVar]);
  }

}

template<class ScalarType>
template<class OtherType>
void CSysMatrix<ScalarType>::EnforceSolutionAtNode(const unsigned long node_i, const OtherType *x_i, CSysVector<OtherType> & b) {

  /*--- Both row and column associated with node i are eliminated (Block_ii = I and all else 0) to preserve eventual symmetry. ---*/
  /*--- The vector is updated with the product of column i by the known (enforced) solution at node i. ---*/

  unsigned long iPoint, iVar, jVar, index, mat_begin;

  /*--- Delete whole row first. ---*/
  for (index = row_ptr[node_i]*nVar*nVar; index < row_ptr[node_i+1]*nVar*nVar; ++index)
    matrix[index] = 0.0;

  /*--- Update b with the column product and delete column. ---*/
  for (iPoint = 0; iPoint < nPoint; ++iPoint) {
    for (index = row_ptr[iPoint]; index < row_ptr[iPoint+1]; ++index) {
      if (col_ind[index] == node_i)
      {
        mat_begin = index*nVar*nVar;

        for(iVar = 0; iVar < nVar; ++iVar)
          for(jVar = 0; jVar < nVar; ++jVar)
            b[iPoint*nVar+iVar] -= matrix[mat_begin+iVar*nVar+jVar] * x_i[jVar];

        /*--- If on diagonal, set diagonal of block to 1, else delete block. ---*/
        if (iPoint == node_i)
          for (iVar = 0; iVar < nVar; ++iVar) matrix[mat_begin+iVar*(nVar+1)] = 1.0;
        else
          for (iVar = 0; iVar < nVar*nVar; iVar++) matrix[mat_begin+iVar] = 0.0;
      }
    }
  }

  /*--- Set know solution in rhs vector. ---*/
  for (iVar = 0; iVar < nVar; iVar++) b[node_i*nVar+iVar] = x_i[iVar];

}

template<class ScalarType>
void CSysMatrix<ScalarType>::BuildPastixPreconditioner(CGeometry *geometry, CConfig *config,
                                                       unsigned short kind_fact, bool transposed) {
#ifdef HAVE_PASTIX
  pastix_wrapper.SetMatrix(nVar,nPoint,nPointDomain,row_ptr,col_ind,matrix);
  pastix_wrapper.Factorize(geometry, config, kind_fact, transposed);
#else
  SU2_MPI::Error("SU2 was not compiled with -DHAVE_PASTIX", CURRENT_FUNCTION);
#endif
}

template<class ScalarType>
void CSysMatrix<ScalarType>::ComputePastixPreconditioner(const CSysVector<ScalarType> & vec, CSysVector<ScalarType> & prod,
                                                         CGeometry *geometry, CConfig *config) {
#ifdef HAVE_PASTIX
  pastix_wrapper.Solve(vec,prod);
  InitiateComms(prod, geometry, config, SOLUTION_MATRIX);
  CompleteComms(prod, geometry, config, SOLUTION_MATRIX);
#else
  SU2_MPI::Error("SU2 was not compiled with -DHAVE_PASTIX", CURRENT_FUNCTION);
#endif
}

#ifdef CODI_REVERSE_TYPE
template<>
void CSysMatrix<su2double>::BuildPastixPreconditioner(CGeometry *geometry, CConfig *config,
                                                      unsigned short kind_fact, bool transposed) {
  SU2_MPI::Error("The PaStiX preconditioner is only available in CSysMatrix<passivedouble>", CURRENT_FUNCTION);
}
template<>
void CSysMatrix<su2double>::ComputePastixPreconditioner(const CSysVector<su2double> & vec, CSysVector<su2double> & prod,
                                                        CGeometry *geometry, CConfig *config) {
  SU2_MPI::Error("The PaStiX preconditioner is only available in CSysMatrix<passivedouble>", CURRENT_FUNCTION);
}
#endif

/*--- Explicit instantiations ---*/
template class CSysMatrix<su2double>;
template void  CSysMatrix<su2double>::InitiateComms(CSysVector<su2double>&, CGeometry*, CConfig*, unsigned short);
template void  CSysMatrix<su2double>::CompleteComms(CSysVector<su2double>&, CGeometry*, CConfig*, unsigned short);
template void  CSysMatrix<su2double>::EnforceSolutionAtNode(unsigned long, const su2double*, CSysVector<su2double>&);

#ifdef CODI_REVERSE_TYPE
template class CSysMatrix<passivedouble>;
template void  CSysMatrix<passivedouble>::InitiateComms(CSysVector<passivedouble>&, CGeometry*, CConfig*, unsigned short);
template void  CSysMatrix<passivedouble>::InitiateComms(CSysVector<su2double>&, CGeometry*, CConfig*, unsigned short);
template void  CSysMatrix<passivedouble>::CompleteComms(CSysVector<passivedouble>&, CGeometry*, CConfig*, unsigned short);
template void  CSysMatrix<passivedouble>::CompleteComms(CSysVector<su2double>&, CGeometry*, CConfig*, unsigned short);
template void  CSysMatrix<passivedouble>::EnforceSolutionAtNode(unsigned long, const passivedouble*, CSysVector<passivedouble>&);
template void  CSysMatrix<passivedouble>::EnforceSolutionAtNode(unsigned long, const su2double*, CSysVector<su2double>&);
#endif
