#ifdef __APPLE__
#    include <Accelerate/Accelerate.h>
#else
#    include <cblas.h>
#endif
#include <float.h>
#include <math.h>
#include "scrappie_matrix.h"
#include "scrappie_stdlib.h"

scrappie_matrix make_scrappie_matrix(int nr, int nc) {
    assert(nr > 0);
    assert(nc > 0);
    // Matrix padded so row length is multiple of 4
    const int nrq = (int)ceil(nr / 4.0);
    scrappie_matrix mat = malloc(sizeof(*mat));
    RETURN_NULL_IF(NULL == mat, NULL);

    mat->nr = nr;
    mat->nrq = nrq;
    mat->nc = nc;

    {
        // Check for overflow to please Coverity scanner
        size_t tmp1 = nrq * sizeof(__m128);
        size_t tmp2 = tmp1 * nc;
        if (tmp1 != 0 && tmp2 / tmp1 != nc) {
            // Have overflow in memory allocation
            free(mat);
            return NULL;
        }
    }

    mat->data.v = aligned_alloc(16, nrq * nc * sizeof(__m128));
    if (NULL == mat->data.v) {
        warnx("Error allocating memory in %s.\n", __func__);
        free(mat);
        return NULL;
    }
    memset(mat->data.v, 0, nrq * nc * sizeof(__m128));
    return mat;
}

scrappie_matrix remake_scrappie_matrix(scrappie_matrix M, int nr, int nc) {
    // Could be made more efficient when there is sufficent memory already allocated
    if ((NULL == M) || (M->nr != nr) || (M->nc != nc)) {
        M = free_scrappie_matrix(M);
        M = make_scrappie_matrix(nr, nc);
    }
    return M;
}

scrappie_matrix copy_scrappie_matrix(const_scrappie_matrix M){
    RETURN_NULL_IF(NULL == M, NULL);
    scrappie_matrix C = make_scrappie_matrix(M->nr, M->nc);
    RETURN_NULL_IF(NULL == C, NULL);
    memcpy(C->data.f, M->data.f, sizeof(__m128) * C->nrq * C->nc);
    return C;
}


void zero_scrappie_matrix(scrappie_matrix M) {
    if (NULL == M) {
        return;
    }
    memset(M->data.f, 0, M->nrq * 4 * M->nc * sizeof(float));
}

scrappie_matrix mat_from_array(const float *x, int nr, int nc) {
    scrappie_matrix res = make_scrappie_matrix(nr, nc);
    RETURN_NULL_IF(NULL == res, NULL);

    for (int col = 0; col < nc; col++) {
        memcpy(res->data.f + col * res->nrq * 4, x + col * nr,
               nr * sizeof(float));
    }
    return res;
}

float * array_from_scrappie_matrix(const_scrappie_matrix mat){
    RETURN_NULL_IF(NULL == mat, NULL);

    const size_t nelt = mat->nr * mat->nc;
    float * res = calloc(nelt, sizeof(float));
    RETURN_NULL_IF(NULL == res, NULL);

    for(size_t c=0 ; c < mat->nc ; c++){
        const size_t offset_out = c * mat->nr;
        const size_t offset_in = c * mat->nrq * 4;
        for(size_t r=0 ; r < mat->nr ; r++){
            res[offset_out + r] = mat->data.f[offset_in + r];
        }
    }

    return res;
}


void fprint_scrappie_matrix(FILE * fh, const char *header,
                            const_scrappie_matrix mat, int nr, int nc,
                            bool include_padding) {
    assert(NULL != fh);
    assert(NULL != mat);
    const int rlim = include_padding ? (4 * mat->nrq) : mat->nr;

    if (nr <= 0 || nr > rlim) {
        nr = rlim;
    }
    if (nc <= 0 || nc > mat->nc) {
        nc = mat->nc;
    }

    if (NULL != header) {
        int ret = fputs(header, fh);
        if (EOF == ret || ret < 0) {
            return;
        }
        fputc('\n', fh);
    }
    for (int c = 0; c < nc; c++) {
        const size_t offset = c * mat->nrq * 4;
        fprintf(fh, "%4d : % 12e", c, mat->data.f[offset]);
        for (int r = 1; r < nr; r++) {
            fprintf(fh, "  % 12e", mat->data.f[offset + r]);
        }
        fputc('\n', fh);
    }
}

scrappie_matrix free_scrappie_matrix(scrappie_matrix mat) {
    if (NULL != mat) {
        free(mat->data.v);
        free(mat);
    }
    return NULL;
}

bool validate_scrappie_matrix(scrappie_matrix mat, float lower,
                              const float upper, const float maskval,
                              const bool only_finite, const char *file,
                              const int line) {
#ifdef NDEBUG
    return true;
}
#else
    if (NULL == mat) {
        return false;
    }
    assert(NULL != mat->data.f);
    assert(mat->nc > 0);
    assert(mat->nr > 0);
    assert(mat->nrq > 0 && (4 * mat->nrq) >= mat->nr);

    const int nc = mat->nc;
    const int nr = mat->nr;
    const int ld = mat->nrq * 4;

    //  Masked values correct
    if (!isnan(maskval)) {
        for (int c = 0; c < nc; ++c) {
            const size_t offset = c * ld;
            for (int r = nr; r < ld; ++r) {
                if (maskval != mat->data.f[offset + r]) {
                    warnx
                        ("%s:%d  Matrix entry [%d,%d] = %f violates masking rules\n",
                         file, line, r, c, mat->data.f[offset + r]);
                    return false;
                }
            }
        }
    }
    //  Check finite
    if (only_finite) {
        for (int c = 0; c < nc; ++c) {
            const size_t offset = c * ld;
            for (int r = 0; r < nr; ++r) {
                if (!isfinite(mat->data.f[offset + r])) {
                    warnx
                        ("%s:%d  Matrix entry [%d,%d] = %f contains a non-finite value\n",
                         file, line, r, c, mat->data.f[offset + r]);
                    return false;
                }
            }
        }
    }
    //  Lower bound
    if (!isnan(lower)) {
        for (int c = 0; c < nc; ++c) {
            const size_t offset = c * ld;
            for (int r = 0; r < nr; ++r) {
                if (mat->data.f[offset + r] + FLT_EPSILON < lower) {
                    warnx
                        ("%s:%d  Matrix entry [%d,%d] = %f (%e) violates lower bound\n",
                         file, line, r, c, mat->data.f[offset + r],
                         mat->data.f[offset + r] - lower);
                    return false;
                }
            }
        }
    }
    //  Upper bound
    if (!isnan(upper)) {
        for (int c = 0; c < nc; ++c) {
            const size_t offset = c * ld;
            for (int r = 0; r < nr; ++r) {
                if (mat->data.f[offset + r] > upper + FLT_EPSILON) {
                    warnx
                        ("%s:%d  Matrix entry [%d,%d] = %f (%e) violates upper bound\n",
                         file, line, r, c, mat->data.f[offset + r],
                         mat->data.f[offset + r] - upper);
                    return false;
                }
            }
        }
    }

    return true;
}
#endif /* NDEBUG */

/**  Check whether two matrices are equal within a given tolerance
 *
 *  @param mat1 A `scrappie_matrix` to compare
 *  @param mat2 A `scrappie_matrix` to compare
 *  @param tol Absolute tolerance to which elements of the matrix should agree
 *
 *  Notes:
 *    The tolerance is absolute; this may not be desirable in all circumstances.
 *    The convention used here is that of equality '=='.  The standard C
 *    sorting functions expect the convention of 0 being equal and non-equality
 *    being defined by negative (less than) and positive (greater than).
 *
 *  @return A boolean of whether the two matrices are equal.
 **/
bool equality_scrappie_matrix(const_scrappie_matrix mat1,
                              const_scrappie_matrix mat2, const float tol) {
    if (NULL == mat1 || NULL == mat2) {
        // One or both matrices are NULL
        if (NULL == mat1 && NULL == mat2) {
            return true;
        }
        return false;
    }
    // Given non-NULL matrices, they should always contain data
    assert(NULL != mat1->data.f);
    assert(NULL != mat2->data.f);

    if (mat1->nc != mat2->nc || mat1->nr != mat2->nr) {
        // Dimension mismatch
        return false;
    }
    //  Given equal dimensions, the following should alway hold
    assert(mat1->nrq == mat2->nrq);

    for (int c = 0; c < mat1->nc; ++c) {
        const int offset = c * 4 * mat1->nrq;
        for (int r = 0; r < mat1->nr; ++r) {
            if (fabsf(mat1->data.f[offset + r] - mat2->data.f[offset + r]) >
                tol) {
                return false;
            }
        }
    }

    return true;
}

scrappie_imatrix make_scrappie_imatrix(int nr, int nc) {
    assert(nr > 0);
    assert(nc > 0);
    // Matrix padded so row length is multiple of 4
    const int nrq = (int)ceil(nr / 4.0);
    scrappie_imatrix mat = malloc(sizeof(*mat));
    RETURN_NULL_IF(NULL == mat, NULL);

    mat->nr = nr;
    mat->nrq = nrq;
    mat->nc = nc;
    mat->data.v = aligned_alloc(16, nrq * nc * sizeof(__m128i));
    if (NULL == mat->data.v) {
        warnx("Error allocating memory in %s.\n", __func__);
        free(mat);
        return NULL;
    }
    memset(mat->data.v, 0, nrq * nc * sizeof(__m128));
    return mat;
}

scrappie_imatrix remake_scrappie_imatrix(scrappie_imatrix M, int nr, int nc) {
    // Could be made more efficient when there is sufficent memory already allocated
    if ((NULL == M) || (M->nr != nr) || (M->nc != nc)) {
        M = free_scrappie_imatrix(M);
        M = make_scrappie_imatrix(nr, nc);
    }
    return M;
}

scrappie_imatrix copy_scrappie_imatrix(const_scrappie_imatrix M){
    RETURN_NULL_IF(NULL == M, NULL);
    scrappie_imatrix C = make_scrappie_imatrix(M->nr, M->nc);
    RETURN_NULL_IF(NULL == C, NULL);
    memcpy(C->data.f, M->data.f, sizeof(__m128i) * C->nrq * C->nc);
    return C;
}

scrappie_imatrix free_scrappie_imatrix(scrappie_imatrix mat) {
    if (NULL != mat) {
        free(mat->data.v);
        free(mat);
    }
    return NULL;
}

void zero_scrappie_imatrix(scrappie_imatrix M) {
    if (NULL == M) {
        return;
    }
    memset(M->data.f, 0, M->nrq * 4 * M->nc * sizeof(int));
}

scrappie_matrix affine_map(const_scrappie_matrix X, const_scrappie_matrix W,
                           const_scrappie_matrix b, scrappie_matrix C) {
    /*  Affine transform C = W^t X + b
     *  X is [nr, nc]
     *  W is [nr, nk]
     *  b is [nk]
     *  C is [nk, nc] or NULL.  If NULL then C is allocated.
     */
    RETURN_NULL_IF(NULL == X, NULL);

    assert(NULL != W);
    assert(NULL != b);
    assert(W->nr == X->nr);

    C = remake_scrappie_matrix(C, W->nc, X->nc);
    RETURN_NULL_IF(NULL == C, NULL);

    /* Copy bias */
    for (int c = 0; c < C->nc; c++) {
        memcpy(C->data.v + c * C->nrq, b->data.v, C->nrq * sizeof(__m128));
    }

    /* Affine transform */
    cblas_sgemm(CblasColMajor, CblasTrans, CblasNoTrans, W->nc, X->nc, W->nr,
                1.0, W->data.f, W->nrq * 4, X->data.f, X->nrq * 4, 1.0,
                C->data.f, C->nrq * 4);

    return C;
}

scrappie_matrix affine_map2(const_scrappie_matrix Xf, const_scrappie_matrix Xb,
                            const_scrappie_matrix Wf, const_scrappie_matrix Wb,
                            const_scrappie_matrix b, scrappie_matrix C) {
    RETURN_NULL_IF(NULL == Xf, NULL);
    RETURN_NULL_IF(NULL == Xb, NULL);

    assert(NULL != Wf);
    assert(NULL != Wb);
    assert(NULL != b);
    assert(Wf->nr == Xf->nr);
    assert(Wb->nr == Xb->nr);
    assert(Xf->nc == Xb->nc);
    assert(Wf->nc == Wb->nc);
    C = remake_scrappie_matrix(C, Wf->nc, Xf->nc);
    RETURN_NULL_IF(NULL == C, NULL);

    /* Copy bias */
    for (int c = 0; c < C->nc; c++) {
        memcpy(C->data.v + c * C->nrq, b->data.v, C->nrq * sizeof(__m128));
    }

    /* Affine transform -- forwards */
    cblas_sgemm(CblasColMajor, CblasTrans, CblasNoTrans, Wf->nc, Xf->nc, Wf->nr,
                1.0, Wf->data.f, Wf->nrq * 4, Xf->data.f, Xf->nrq * 4, 1.0,
                C->data.f, C->nrq * 4);
    /* Affine transform -- backwards */
    cblas_sgemm(CblasColMajor, CblasTrans, CblasNoTrans, Wb->nc, Xb->nc, Wb->nr,
                1.0, Wb->data.f, Wb->nrq * 4, Xb->data.f, Xb->nrq * 4, 1.0,
                C->data.f, C->nrq * 4);
    return C;
}

void row_normalise_inplace(scrappie_matrix C) {
    if (NULL == C) {
        // Input NULL due to earlier failure.  Propagate
        return;
    }
    const int i = C->nrq * 4 - C->nr;
    const __m128 mask = _mm_cmpgt_ps(_mm_set_ps(i >= 1, i >= 2, i >= 3, 0), _mm_set1_ps(0.0f));
    for (int col = 0; col < C->nc; col++) {
        const size_t offset = col * C->nrq;
        __m128 sum = C->data.v[offset];
        for (int row = 1; row < C->nrq; row++) {
            sum += C->data.v[offset + row];
        }
        sum -= _mm_and_ps(C->data.v[offset + C->nrq - 1], mask);
        const __m128 psum = _mm_hadd_ps(sum, sum);
        const __m128 tsum = _mm_hadd_ps(psum, psum);

        const __m128 tsum_recip = _mm_set1_ps(1.0f) / tsum;
        for (int row = 0; row < C->nrq; row++) {
            C->data.v[offset + row] *= tsum_recip;
        }
    }
}

float max_scrappie_matrix(const_scrappie_matrix x) {
    if (NULL == x) {
        // Input NULL due to earlier failure.  Propagate
        return NAN;
    }
    float amax = x->data.f[0];
    for (int col = 0; col < x->nc; col++) {
        const size_t offset = col * x->nrq * 4;
        for (int r = 0; r < x->nr; r++) {
            if (amax < x->data.f[offset + r]) {
                amax = x->data.f[offset + r];
            }
        }
    }
    return amax;
}

float min_scrappie_matrix(const_scrappie_matrix x) {
    if (NULL == x) {
        // Input NULL due to earlier failure.  Propagate
        return NAN;
    }
    float amin = x->data.f[0];
    for (int col = 0; col < x->nc; col++) {
        const size_t offset = col * x->nrq * 4;
        for (int r = 0; r < x->nr; r++) {
            if (amin < x->data.f[offset + r]) {
                amin = x->data.f[offset + r];
            }
        }
    }
    return amin;
}

int argmax_scrappie_matrix(const_scrappie_matrix x) {
    if (NULL == x) {
        // Input NULL due to earlier failure.  Propagate
        return -1;
    }
    float amax = x->data.f[0];
    int imax = 0;

    for (int col = 0; col < x->nc; col++) {
        const size_t offset = col * x->nrq * 4;
        for (int r = 0; r < x->nr; r++) {
            if (amax < x->data.f[offset + r]) {
                amax = x->data.f[offset + r];
                imax = offset + r;
            }
        }
    }
    return imax;
}

int argmin_scrappie_matrix(const_scrappie_matrix x) {
    if (NULL == x) {
        // Input NULL due to earlier failure.  Propagate
        return -1;
    }
    float amin = x->data.f[0];
    int imin = 0;

    for (int col = 0; col < x->nc; col++) {
        const size_t offset = col * x->nrq * 4;
        for (int r = 0; r < x->nr; r++) {
            if (amin < x->data.f[offset + r]) {
                amin = x->data.f[offset + r];
                imin = offset + r;
            }
        }
    }
    return imin;
}

bool validate_vector(float *vec, const float n, const float lower,
                     const float upper, const char *file, const int line) {
#ifdef NDEBUG
    return true;
}
#else
    if (NULL == vec) {
        return false;
    }
    //  Lower bound
    if (!isnan(lower)) {
        for (int i = 0; i < n; ++i) {
            if (lower > vec[i]) {
                warnx("%s:%d  Vector entry %d = %f violates lower bound\n",
                      file, line, i, vec[i]);
                return false;
            }
        }
    }
    //  Upper bound
    if (!isnan(upper)) {
        for (int i = 0; i < n; ++i) {
            if (upper < vec[i]) {
                warnx("%s:%d  Vector entry %d = %f violates upper bound\n",
                      file, line, i, vec[i]);
                return false;
            }
        }
    }

    return true;
}
#endif /* NDEBUG */

bool validate_ivector(int *vec, const int n, const int lower, const int upper,
                      const char *file, const int line) {
#ifdef NDEBUG
    return true;
}
#else
    if (NULL == vec) {
        return false;
    }
    //  Lower bound
    for (int i = 0; i < n; ++i) {
        if (lower > vec[i]) {
            warnx("%s:%d  Vector entry %d = %d violates lower bound\n", file,
                  line, i, vec[i]);
            return false;
        }
    }

    //  Upper bound
    for (int i = 0; i < n; ++i) {
        if (upper < vec[i]) {
            warnx("%s:%d  Vector entry %d = %d violates upper bound\n", file,
                  line, i, vec[i]);
            return false;
        }
    }

    return true;
}
#endif /* NDEBUG */
