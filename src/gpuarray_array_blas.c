#include <stdlib.h>
#include "gpuarray/blas.h"
#include "gpuarray/buffer_blas.h"
#include "gpuarray/types.h"
#include "gpuarray/util.h"

#include "private.h"
#include "util/error.h"

int GpuArray_rdot(GpuArray *X, GpuArray *Y,
                  GpuArray *Z, int nocopy) {
    GpuArray *Xp = X;
    GpuArray copyX;
    GpuArray *Yp = Y;
    GpuArray copyY;
    GpuArray *Zp = Z;
    size_t n;
    gpucontext *ctx = gpudata_context(Xp->data);
    size_t elsize;
    int err;

  if (X->typecode != GA_HALF &&
      X->typecode != GA_FLOAT &&
      X->typecode != GA_DOUBLE)
  return error_set(ctx->err, GA_INVALID_ERROR, "Data type not supported");

  if (X->nd != 1 || Y->nd != 1 || Z->nd != 0)
    return error_fmt(ctx->err, GA_VALUE_ERROR,
                     "Wrong number of dimensions: X->nd = %u (expected 1), Y->nd = %u (expected 1), Z->nd = %u (expected 0)",
                     X->nd, Y->nd, Z->nd);
  if (X->typecode != Y->typecode || X->typecode != Z->typecode)
    error_set(ctx->err, GA_VALUE_ERROR, "Inconsistent dtypes");
  n = X->dimensions[0];
  if (!(X->flags & GA_ALIGNED) || !(Y->flags & GA_ALIGNED) ||
      !(Z->flags & GA_ALIGNED))
    return error_set(ctx->err, GA_UNALIGNED_ERROR, "One of the inputs is unaligned");
  if (X->dimensions[0] != Y->dimensions[0])
      return error_fmt(ctx->err, GA_VALUE_ERROR,
                       "Shape mismatch: X->dimensions[0] = %d != Y->dimensions[0] = %d",
                       X->dimensions[0], Y->dimensions[0]);

  elsize = gpuarray_get_elsize(X->typecode);
  if (X->strides[0] < 0) {
    if (nocopy)
      return error_set(ctx->err, GA_COPY_ERROR, "Copy required for X");
    else {
      err = GpuArray_copy(&copyX, X, GA_ANY_ORDER);
      if (err != GA_NO_ERROR)
        goto cleanup;
      Xp = &copyX;
    }
  }
  if (Y->strides[0] < 0) {
    if (nocopy)
      return error_set(ctx->err, GA_COPY_ERROR, "Copy required for Y");
    else {
      err = GpuArray_copy(&copyY, Y, GA_ANY_ORDER);
      if (err != GA_NO_ERROR)
        goto cleanup;
      Yp = &copyY;
    }
  }

  err = gpublas_setup(ctx);
  if (err != GA_NO_ERROR)
      goto cleanup;

  switch (Xp->typecode) {
      case GA_HALF:
          err = gpublas_hdot(
                  n,
                  Xp->data, Xp->offset / elsize, Xp->strides[0] / elsize,
                  Yp->data, Yp->offset / elsize, Yp->strides[0] / elsize,
                  Zp->data, Zp->offset / elsize);
          break;
      case GA_FLOAT:
          err = gpublas_sdot(
                  n,
                  Xp->data, Xp->offset / elsize, Xp->strides[0] / elsize,
                  Yp->data, Yp->offset / elsize, Yp->strides[0] / elsize,
                  Zp->data, Zp->offset / elsize);
          break;
      case GA_DOUBLE:
          err = gpublas_ddot(
                  n,
                  Xp->data, Xp->offset / elsize, Xp->strides[0] / elsize,
                  Yp->data, Yp->offset / elsize, Yp->strides[0] / elsize,
                  Zp->data, Zp->offset / elsize);
          break;
  }
  cleanup:
   if (Xp == &copyX)
       GpuArray_clear(&copyX);
   if (Yp == &copyY)
       GpuArray_clear(&copyY);
   return err;
}

int GpuArray_rgemv(cb_transpose transA, double alpha, GpuArray *A,
                   GpuArray *X, double beta, GpuArray *Y, int nocopy) {
  GpuArray *Ap = A;
  GpuArray copyA;
  GpuArray *Xp = X;
  GpuArray copyX;
  GpuArray *Yp = Y;
  gpucontext *ctx = gpudata_context(Ap->data);
  size_t elsize;
  size_t m, n, lda;
  cb_order o;
  int err;

  if (A->typecode != GA_HALF &&
      A->typecode != GA_FLOAT &&
      A->typecode != GA_DOUBLE)
    return error_set(ctx->err, GA_INVALID_ERROR, "Unsupported dtype");

  if (A->nd != 2 || X->nd != 1 || Y->nd != 1)
    return error_fmt(ctx->err, GA_VALUE_ERROR,
                     "Wrong number of dimensions: A->nd = %u (expected 2), X->nd = %u (expected 1), Y->nd = %u (expected 1)",
                     A->nd, X->nd, Y->nd);
  if (X->typecode != A->typecode || Y->typecode != A->typecode)
    return error_set(ctx->err, GA_VALUE_ERROR, "Inconsistent dtypes");

  if (!(A->flags & GA_ALIGNED) || !(X->flags & GA_ALIGNED) ||
      !(Y->flags & GA_ALIGNED))
    return error_set(ctx->err, GA_UNALIGNED_ERROR, "Unaligned inputs");

  if (transA == cb_no_trans) {
    m = A->dimensions[0];
    n = A->dimensions[1];
  } else {
    m = A->dimensions[1];
    n = A->dimensions[0];
  }

  if (Y->dimensions[0] != m || X->dimensions[0] != n)
    return error_set(ctx->err, GA_VALUE_ERROR, "Inconsistent shapes");

  m = A->dimensions[0];
  n = A->dimensions[1];

  elsize = gpuarray_get_elsize(A->typecode);

  if (!GpuArray_ISONESEGMENT(A)) {
    if (nocopy)
      return error_set(ctx->err, GA_COPY_ERROR, "Copy required for A");
    else {
      err = GpuArray_copy(&copyA, A, GA_F_ORDER);
      if (err != GA_NO_ERROR)
        goto cleanup;
      Ap = &copyA;
    }
  }
  if (X->strides[0] < 0) {
    if (nocopy)
      return error_set(ctx->err, GA_COPY_ERROR, "Copy required for X");
    else {
      err = GpuArray_copy(&copyX, X, GA_ANY_ORDER);
      if (err != GA_NO_ERROR)
        goto cleanup;
      Xp = &copyX;
    }
  }
  if (Y->strides[0] < 0) {
    err = error_set(ctx->err, GA_VALUE_ERROR, "Negative strides for Y");
    goto cleanup;
  }

  if (Ap->flags & GA_F_CONTIGUOUS) {
    o = cb_fortran;
    lda = Ap->dimensions[0];
  } else if (Ap->flags & GA_C_CONTIGUOUS) {
    o = cb_c;
    lda = Ap->dimensions[1];
  } else {
    /* Might be worth looking at making degenerate matrices (1xn) work here. */
    err = error_set(ctx->err, GA_VALUE_ERROR, "Noncontiguous A");
    goto cleanup;
  }

  err = gpublas_setup(ctx);
  if (err != GA_NO_ERROR)
    goto cleanup;

  switch (Ap->typecode) {
  case GA_HALF:
    err = gpublas_hgemv(o, transA, m, n, (float)alpha, Ap->data, Ap->offset / elsize, lda, Xp->data, Xp->offset / elsize, Xp->strides[0] / elsize, (float)beta, Yp->data, Yp->offset / elsize, Yp->strides[0] / elsize);
    break;
  case GA_FLOAT:
    err = gpublas_sgemv(o, transA, m, n, (float)alpha, Ap->data, Ap->offset / elsize, lda, Xp->data, Xp->offset / elsize, Xp->strides[0] / elsize, (float)beta, Yp->data, Yp->offset / elsize, Yp->strides[0] / elsize);
    break;
  case GA_DOUBLE:
    err = gpublas_dgemv(o, transA, m, n, (double)alpha, Ap->data, Ap->offset / elsize, lda, Xp->data, Xp->offset / elsize, Xp->strides[0] / elsize, (double)beta, Yp->data, Yp->offset / elsize, Yp->strides[0] / elsize);
    break;
  }
 cleanup:
  if (Ap == &copyA)
    GpuArray_clear(&copyA);
  if (Xp == &copyX)
    GpuArray_clear(&copyX);
  return err;
}

int GpuArray_rgemm(cb_transpose transA, cb_transpose transB, double alpha,
                   GpuArray *A, GpuArray *B, double beta, GpuArray *C,
                   int nocopy) {
  GpuArray *Ap = A;
  GpuArray copyA;
  GpuArray *Bp = B;
  GpuArray copyB;
  GpuArray *Cp = C;
  gpucontext *ctx = gpudata_context(Ap->data);
  size_t elsize;
  size_t m, n, k, lda, ldb, ldc;
  cb_order o;
  int err;

  if (A->typecode != GA_HALF && A->typecode != GA_FLOAT &&
      A->typecode != GA_DOUBLE)
    return error_set(ctx->err, GA_INVALID_ERROR, "Unsupported dtype");

  if (A->nd != 2 || B->nd != 2 || C->nd != 2)
    return error_fmt(ctx->err, GA_VALUE_ERROR,
                     "Wrong number of dimensions: A->nd = %u (expected 2), B->nd = %u (expected 2), C->nd = %u (expected 2)",
                     A->nd, B->nd, C->nd);
  if (B->typecode != A->typecode || C->typecode != A->typecode)
    return error_set(ctx->err, GA_VALUE_ERROR, "Inconsistent dtypes");

  if (!(A->flags & GA_ALIGNED) || !(B->flags & GA_ALIGNED) ||
      !(C->flags & GA_ALIGNED))
    return error_set(ctx->err, GA_UNALIGNED_ERROR, "Unaligned inputs");

  if (transA == cb_no_trans) {
    m = A->dimensions[0];
    k = A->dimensions[1];
  } else {
    m = A->dimensions[1];
    k = A->dimensions[0];
  }

  if (transB == cb_no_trans) {
    n = B->dimensions[1];
    if (B->dimensions[0] != k)
      return error_set(ctx->err, GA_VALUE_ERROR, "mismatched shapes");
  } else {
    n = B->dimensions[0];
    if (B->dimensions[1] != k)
      return error_set(ctx->err, GA_VALUE_ERROR, "mismatched shapes");
  }

  if (C->dimensions[0] != m || C->dimensions[1] != n)
    return error_set(ctx->err, GA_VALUE_ERROR, "mismatched shapes");

  elsize = gpuarray_get_elsize(A->typecode);

  if (!GpuArray_ISONESEGMENT(A)) {
    if (nocopy)
      return error_set(ctx->err, GA_COPY_ERROR, "Need copy for A");
    else {
      err = GpuArray_copy(&copyA, A, GA_F_ORDER);
      if (err != GA_NO_ERROR)
        goto cleanup;
      Ap = &copyA;
    }
  }
  if (!GpuArray_ISONESEGMENT(B)) {
    if (nocopy)
      return error_set(ctx->err, GA_COPY_ERROR, "Need copy for B");
    else {
      err = GpuArray_copy(&copyB, B, GA_F_ORDER);
      if (err != GA_NO_ERROR)
        goto cleanup;
      Bp = &copyB;
    }
  }
  if (!GpuArray_ISONESEGMENT(C)) {
    err = error_set(ctx->err, GA_VALUE_ERROR, "Noncontiguous C");
    goto cleanup;
  }

  if (Cp->flags & GA_F_CONTIGUOUS) {
    o = cb_fortran;
    ldc = Cp->dimensions[0];
  } else if (Cp->flags & GA_C_CONTIGUOUS) {
    o = cb_c;
    ldc = Cp->dimensions[1];
  } else {
    err = error_set(ctx->err, GA_VALUE_ERROR, "Noncontiguous C");
    goto cleanup;
  }
  if (Ap->flags & GA_F_CONTIGUOUS) {
    lda = Ap->dimensions[0];
    if (o == cb_c) {
      if (transA == cb_no_trans)
        transA = cb_trans;
      else
        transA = cb_no_trans;
    }
  } else if (Ap->flags & GA_C_CONTIGUOUS) {
    lda = Ap->dimensions[1];
    if (o == cb_fortran) {
      if (transA == cb_no_trans)
        transA = cb_trans;
      else
        transA = cb_no_trans;
    }
  } else {
    err = error_set(ctx->err, GA_VALUE_ERROR, "Noncontiguous A");
    goto cleanup;
  }
  if (Bp->flags & GA_F_CONTIGUOUS) {
    ldb = Bp->dimensions[0];
    if (o == cb_c) {
      if (transB == cb_no_trans)
        transB = cb_trans;
      else
        transB = cb_no_trans;
    }
  } else if (Bp->flags & GA_C_CONTIGUOUS) {
    ldb = Bp->dimensions[1];
    if (o == cb_fortran) {
      if (transB == cb_no_trans)
        transB = cb_trans;
      else
        transB = cb_no_trans;
    }
  } else {
    err = error_set(ctx->err, GA_VALUE_ERROR, "Noncontiguous B");
    goto cleanup;
  }

  ctx = gpudata_context(Ap->data);
  err = gpublas_setup(ctx);
  if (err != GA_NO_ERROR)
    goto cleanup;

  switch (Ap->typecode) {
  case GA_HALF:
      err = gpublas_hgemm(o, transA, transB, m, n, k, (float)alpha, Ap->data, Ap->offset / elsize, lda, Bp->data, Bp->offset / elsize, ldb, (float)beta, Cp->data, Cp->offset / elsize, ldc);
    break;
  case GA_FLOAT:
    err = gpublas_sgemm(o, transA, transB, m, n, k, (float)alpha, Ap->data, Ap->offset / elsize, lda, Bp->data, Bp->offset / elsize, ldb, (float)beta, Cp->data, Cp->offset / elsize, ldc);
    break;
  case GA_DOUBLE:
    err = gpublas_dgemm(o, transA, transB, m, n, k, (double)alpha, Ap->data, Ap->offset / elsize, lda, Bp->data, Bp->offset / elsize, ldb, (double)beta, Cp->data, Cp->offset / elsize, ldc);
    break;
  }

 cleanup:
  if (Ap == &copyA)
    GpuArray_clear(&copyA);
  if (Bp == &copyB)
    GpuArray_clear(&copyB);
  return err;
}

int GpuArray_rger(double alpha, GpuArray *X, GpuArray *Y, GpuArray *A,
                  int nocopy) {
  GpuArray *Xp = X;
  GpuArray copyX;
  GpuArray *Yp = Y;
  GpuArray copyY;
  GpuArray *Ap = A;
  gpucontext *ctx = gpudata_context(Xp->data);
  size_t elsize;
  size_t m, n, lda;
  cb_order o;
  int err;

  if (X->typecode != GA_HALF && X->typecode != GA_FLOAT &&
      X->typecode != GA_DOUBLE)
    return error_set(ctx->err, GA_INVALID_ERROR, "Unsupported dtype");

  if (X->nd != 1 || Y->nd != 1 || A->nd != 2)
    return error_fmt(ctx->err, GA_VALUE_ERROR,
                     "Wrong number of dimensions: X->nd = %u (expected 1), Y->nd = %u (expected 1), A->nd = %u (expected 2)",
                     X->nd, Y->nd, A->nd);
  if (Y->typecode != X->typecode || A->typecode != X->typecode)
    return error_set(ctx->err, GA_VALUE_ERROR, "Inconsistent dtypes");

  if (!(X->flags & GA_ALIGNED) || !(Y->flags & GA_ALIGNED) ||
      !(A->flags & GA_ALIGNED))
    return error_set(ctx->err, GA_UNALIGNED_ERROR, "Unaligned inputs");

  m = X->dimensions[0];
  n = Y->dimensions[0];
  if (A->dimensions[0] != m || A->dimensions[1] != n)
    return error_set(ctx->err, GA_VALUE_ERROR, "Incompatible shapes");

  elsize = gpuarray_get_elsize(X->typecode);

  if (X->strides[0] < 0) {
    if (nocopy)
      return error_set(ctx->err, GA_COPY_ERROR, "Need copy for X");
    else {
      err = GpuArray_copy(&copyX, X, GA_ANY_ORDER);
      if (err != GA_NO_ERROR)
        goto cleanup;
      Xp = &copyX;
    }
  }
  if (Y->strides[0] < 0) {
    if (nocopy)
      return error_set(ctx->err, GA_COPY_ERROR, "Need copy for Y");
    else {
      err = GpuArray_copy(&copyY, Y, GA_ANY_ORDER);
      if (err != GA_NO_ERROR)
        goto cleanup;
      Yp = &copyY;
    }
  }
  if (!GpuArray_ISONESEGMENT(A)) {
    err = error_set(ctx->err, GA_VALUE_ERROR, "Noncontiguous A");
    goto cleanup;
  }

  if (Ap->flags & GA_F_CONTIGUOUS) {
    o = cb_fortran;
    lda = Ap->dimensions[0];
  } else if (Ap->flags & GA_C_CONTIGUOUS) {
    o = cb_c;
    lda = Ap->dimensions[1];
  } else {
    /* Might be worth looking at making degenerate matrices (1xn) work here. */
    err = error_set(ctx->err, GA_VALUE_ERROR, "Noncontiguous A");
    goto cleanup;
  }

  ctx = gpudata_context(Xp->data);
  err = gpublas_setup(ctx);
  if (err != GA_NO_ERROR)
    goto cleanup;

  switch(Xp->typecode) {
  case GA_HALF:
      err = gpublas_hger(o, m, n, (float)alpha, Xp->data, Xp->offset / elsize, Xp->strides[0] / elsize, Yp->data, Yp->offset / elsize, Yp->strides[0] / elsize, Ap->data, Ap->offset / elsize, lda);
    break;
  case GA_FLOAT:
    err = gpublas_sger(o, m, n, (float)alpha, Xp->data, Xp->offset / elsize, Xp->strides[0] / elsize, Yp->data, Yp->offset / elsize, Yp->strides[0] / elsize, Ap->data, Ap->offset / elsize, lda);
    break;
  case GA_DOUBLE:
    err = gpublas_dger(o, m, n, (double)alpha, Xp->data, Xp->offset / elsize, Xp->strides[0] / elsize, Yp->data, Yp->offset / elsize, Yp->strides[0] / elsize, Ap->data, Ap->offset / elsize, lda);
    break;
  }

 cleanup:
  if (Xp == &copyX)
    GpuArray_clear(&copyX);
  if (Yp == &copyY)
    GpuArray_clear(&copyY);
  return err;
}

static inline int is_last_2d_contiguous(const GpuArray *a) {
  ssize_t size = GpuArray_ITEMSIZE(a);

  if (GpuArray_IS_C_CONTIGUOUS(a))
    return 1; // C contiguous

  if (a->strides[a->nd - 2] <= 0 || a->strides[a->nd - 1] <= 0)
    return 0;

  if (a->strides[a->nd - 2] == size)
    return 2; // F contiguous
  if (a->strides[a->nd - 1] == size)
    return 1; // C contiguous

  return 0;
}

int GpuArray_rgemmBatch_3d(cb_transpose transA, cb_transpose transB, double alpha,
                           GpuArray *A, GpuArray *B, double beta, GpuArray *C,
                           int nocopy) {
  GpuArray *Ap = A;
  GpuArray copyA;
  GpuArray *Bp = B;
  GpuArray copyB;
  GpuArray *Cp = C;
  gpucontext *ctx = gpudata_context(A->data);
  size_t elsize;
  size_t batchCount, m, n, k, lda, ldb, ldc;
  cb_order o;
  int cA, cB, cC;
  int err;

  if (A->typecode != GA_FLOAT && A->typecode != GA_DOUBLE && A->typecode != GA_HALF)
    return error_set(ctx->err, GA_INVALID_ERROR, "Unsupported dtype");

  if (A->nd != 3 || B->nd != 3 || C->nd != 3)
    return error_fmt(ctx->err, GA_VALUE_ERROR,
                     "Wrong number of dimensions: A->nd = %u (expected 3), B->nd = %u (expected 3), C->nd = %u (expected 3)",
                     A->nd, B->nd, C->nd);
  if (B->typecode != A->typecode || C->typecode != A->typecode)
    return error_set(ctx->err, GA_VALUE_ERROR, "Inconsistent dtypes");

  if (!(A->flags & GA_ALIGNED) || !(B->flags & GA_ALIGNED) ||
      !(C->flags & GA_ALIGNED))
    return error_set(ctx->err, GA_UNALIGNED_ERROR, "Unaligned input");

  batchCount = A->dimensions[0];
  if (B->dimensions[0] != batchCount || C->dimensions[0] != batchCount)
    return error_set(ctx->err, GA_VALUE_ERROR, "Mismatched first dimension");

  if (transA == cb_no_trans) {
    m = A->dimensions[1];
    k = A->dimensions[2];
  } else {
    m = A->dimensions[2];
    k = A->dimensions[1];
  }

  if (transB == cb_no_trans) {
    n = B->dimensions[2];
    if (B->dimensions[1] != k)
      return error_set(ctx->err, GA_VALUE_ERROR, "Mismatched shape");
  } else {
    n = B->dimensions[1];
    if (B->dimensions[2] != k)
      return error_set(ctx->err, GA_VALUE_ERROR, "Mismatched shape");
  }

  if (C->dimensions[1] != m || C->dimensions[2] != n)
    return error_set(ctx->err, GA_VALUE_ERROR, "Mismatched shape");

  elsize = gpuarray_get_elsize(A->typecode);

  cA = is_last_2d_contiguous(A);
  if (!cA) {
    if (nocopy)
      return error_set(ctx->err, GA_COPY_ERROR, "Need copy for A");
    else {
      err = GpuArray_copy(&copyA, A, GA_C_ORDER);
      cA = 1;
      if (err != GA_NO_ERROR)
        goto cleanup;
      Ap = &copyA;
    }
  }
  cB = is_last_2d_contiguous(B);
  if (!cB) {
    if (nocopy)
      return error_set(ctx->err, GA_COPY_ERROR, "Need copy for B");
    else {
      err = GpuArray_copy(&copyB, B, GA_C_ORDER);
      cB = 1;
      if (err != GA_NO_ERROR)
        goto cleanup;
      Bp = &copyB;
    }
  }
  cC = is_last_2d_contiguous(C);
  if (!cC) {
    err = error_set(ctx->err, GA_VALUE_ERROR, "Noncontiguous last 2d C");
    goto cleanup;
  }

  if (cC == 2) {
    o = cb_fortran;
    ldc = Cp->dimensions[2] > 1
          ? Cp->strides[2] / elsize
          : Cp->dimensions[1];
  } else if (cC == 1) {
    o = cb_c;
    ldc = Cp->dimensions[1] > 1
          ? Cp->strides[1] / elsize
          : Cp->dimensions[2];
  } else {
    err = error_set(ctx->err, GA_MISC_ERROR, "Invalid internal result for C");
    goto cleanup;
  }
  if (cA == 2) {
    lda = Ap->dimensions[2] > 1
          ? Ap->strides[2] / elsize
          : Ap->dimensions[1];
    if (o == cb_c) {
      if (transA == cb_no_trans)
        transA = cb_trans;
      else
        transA = cb_no_trans;
    }
  } else if (cA == 1) {
    lda = Ap->dimensions[1] > 1
          ? Ap->strides[1] / elsize
          : Ap->dimensions[2];
    if (o == cb_fortran) {
      if (transA == cb_no_trans)
        transA = cb_trans;
      else
        transA = cb_no_trans;
    }
  } else {
    err = error_set(ctx->err, GA_MISC_ERROR, "Invalid internal result for A");
    goto cleanup;
  }
  if (cB == 2) {
    ldb = Bp->dimensions[2] > 1
          ? Bp->strides[2] / elsize
          : Bp->dimensions[1];
    if (o == cb_c) {
      if (transB == cb_no_trans)
        transB = cb_trans;
      else
        transB = cb_no_trans;
    }
  } else if (cB == 1) {
    ldb = Bp->dimensions[1] > 1
          ? Bp->strides[1] / elsize
          : Bp->dimensions[2];
    if (o == cb_fortran) {
      if (transB == cb_no_trans)
        transB = cb_trans;
      else
        transB = cb_no_trans;
    }
  } else {
    err = error_set(ctx->err, GA_MISC_ERROR, "Invalid internal result for B");
    goto cleanup;
  }

  ctx = gpudata_context(Ap->data);
  err = gpublas_setup(ctx);
  if (err != GA_NO_ERROR)
    goto cleanup;

  switch (C->typecode) {
  case GA_HALF:
    err = gpublas_hgemm3D(o, transA, transB, m, n, k, (float)alpha,
                          Ap->data, Ap->offset/elsize, lda, Ap->strides[0]/elsize,
                          Bp->data, Bp->offset/elsize, ldb, Bp->strides[0]/elsize,
                          (float)beta,
                          Cp->data, Cp->offset/elsize, ldc, Cp->strides[0]/elsize,
                          batchCount, 0);
    break;
  case GA_FLOAT:
    err = gpublas_sgemm3D(o, transA, transB, m, n, k, (float)alpha,
                          Ap->data, Ap->offset/elsize, lda, Ap->strides[0]/elsize,
                          Bp->data, Bp->offset/elsize, ldb, Bp->strides[0]/elsize,
                          (float)beta,
                          Cp->data, Cp->offset/elsize, ldc, Cp->strides[0]/elsize,
                          batchCount, 0);
    break;
  case GA_DOUBLE:
    err = gpublas_dgemm3D(o, transA, transB, m, n, k, (double)alpha,
                          Ap->data, Ap->offset/elsize, lda, Ap->strides[0]/elsize,
                          Bp->data, Bp->offset/elsize, ldb, Bp->strides[0]/elsize,
                          (double)beta,
                          Cp->data, Cp->offset/elsize, ldc, Cp->strides[0]/elsize,
                          batchCount, 0);
    break;
  }

  if (err == GA_DEVSUP_ERROR) {
    gpudata **A_datas = NULL, **B_datas = NULL, **C_datas = NULL;
    size_t *A_offsets = NULL, *B_offsets = NULL, *C_offsets = NULL;
    size_t i;

    A_datas = (gpudata**)malloc(batchCount * sizeof(gpudata*));
    B_datas = (gpudata**)malloc(batchCount * sizeof(gpudata*));
    C_datas = (gpudata**)malloc(batchCount * sizeof(gpudata*));

    A_offsets = (size_t*)malloc(batchCount * sizeof(size_t));
    B_offsets = (size_t*)malloc(batchCount * sizeof(size_t));
    C_offsets = (size_t*)malloc(batchCount * sizeof(size_t));

    if (A_datas == NULL || B_datas == NULL || C_datas == NULL ||
        A_offsets == NULL || B_offsets == NULL || C_offsets) {
      err = error_sys(ctx->err, "malloc");
      goto old_cleanup;
    }

    for (i = 0; i < batchCount; i++) {
      A_datas[i] = Ap->data;
      B_datas[i] = Bp->data;
      C_datas[i] = Cp->data;
      A_offsets[i] = (Ap->offset + i * Ap->strides[0]) / elsize;
      B_offsets[i] = (Bp->offset + i * Bp->strides[0]) / elsize;
      C_offsets[i] = (Cp->offset + i * Cp->strides[0]) / elsize;
    }

    switch (C->typecode) {
      case GA_HALF:
        err = gpublas_hgemmBatch(o, transA, transB, m, n, k, (float)alpha,
                                 A_datas, A_offsets, lda,
                                 B_datas, B_offsets, ldb,
                                 (float)beta,
                                 C_datas, C_offsets, ldc, batchCount, 0);
        break;
      case GA_FLOAT:
        err = gpublas_sgemmBatch(o, transA, transB, m, n, k, (float)alpha,
                                 A_datas, A_offsets, lda,
                                 B_datas, B_offsets, ldb,
                                 (float)beta,
                                 C_datas, C_offsets, ldc, batchCount, 0);
        break;
      case GA_DOUBLE:
        err = gpublas_dgemmBatch(o, transA, transB, m, n, k, (double)alpha,
                                 A_datas, A_offsets, lda,
                                 B_datas, B_offsets, ldb,
                                 (double)beta,
                                 C_datas, C_offsets, ldc, batchCount, 0);
        break;
    }
  old_cleanup:
    free(A_datas); free(B_datas); free(C_datas);
    free(A_offsets); free(B_offsets); free(C_offsets);
  }

  cleanup:
  if (Ap == &copyA)
    GpuArray_clear(&copyA);
  if (Bp == &copyB)
    GpuArray_clear(&copyB);
  return err;
}
