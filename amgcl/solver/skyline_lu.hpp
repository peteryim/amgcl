#ifndef AMGCL_SOLVER_SKYLINE_LU_HPP
#define AMGCL_SOLVER_SKYLINE_LU_HPP

/*
The MIT License

Copyright (c) 2012-2014 Denis Demidov <dennis.demidov@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

/**
\file   amgcl/solver/skyline_lu.hpp
\author Denis Demidov <dennis.demidov@gmail.com>
\brief  Skyline LU factorization solver.

The code is adopted from Kratos project http://www.cimne.com/kratos. The
original code came with the following copyright notice:
\verbatim
Kratos Multi-Physics

Copyright (c) 2012, Pooyan Dadvand, Riccardo Rossi, CIMNE (International Center for Numerical Methods in Engineering)
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.
    Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.
    All advertising materials mentioning features or use of this software must
    display the following acknowledgement:
    This product includes Kratos Multi-Physics technology.
    Neither the name of the CIMNE nor the names of its contributors may be used
    to endorse or promote products derived from this software without specific
    prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED ANDON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT(INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THISSOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\endverbatim
*/

#include <vector>
#include <algorithm>

#include <boost/range/algorithm.hpp>
#include <boost/range/algorithm_ext.hpp>

#include <amgcl/backend/interface.hpp>

namespace amgcl {
namespace solver {

namespace matrix_permutation {

struct none {
    template <class Matrix>
    static void get(const Matrix&, std::vector<int> &perm) {
        boost::iota(perm, 0);
    }
};

template <bool reverse = false>
struct CuthillMcKee {
    template <class Matrix>
    static void get(const Matrix &A, std::vector<int> &perm) {
        typedef typename backend::row_iterator<Matrix>::type row_iterator;
        const int n = backend::rows(A);

        /* The data structure used to sort and traverse the level sets:
         *
         * The current level set is currentLevelSet;
         * In this level set, there are nodes with degrees from 0 (not really
         * useful) to maxDegreeInCurrentLevelSet.
         * firstWithDegree[i] points to a node with degree i, or to -1 if it
         * does not exist. nextSameDegree[firstWithDegree[i]] points to the
         * second node with that degree, etc.
         * While the level set is being traversed, the structure for the next
         * level set is generated; nMDICLS will be the next
         * maxDegreeInCurrentLevelSet and nFirstWithDegree will be
         * firstWithDegree.
         */
        int initialNode = 0; // node to start search
        int maxDegree   = 0;

        std::vector<int> degree(n);
        std::vector<int> levelSet(n, 0);
        std::vector<int> nextSameDegree(n, -1);

        for(int i = 0; i < n; ++i) {
            degree[i] = backend::row_nonzeros(A, i);
            maxDegree = std::max(maxDegree, degree[i]);
        }

        std::vector<int> firstWithDegree(maxDegree + 1, -1);
        std::vector<int> nFirstWithDegree(maxDegree + 1);

        // Initialize the first level set, made up by initialNode alone
        perm[0] = initialNode;
        int currentLevelSet = 1;
        levelSet[initialNode] = currentLevelSet;
        int maxDegreeInCurrentLevelSet = degree[initialNode];
        firstWithDegree[maxDegreeInCurrentLevelSet] = initialNode;

        // Main loop
        for (int next = 1; next < n; ) {
            int nMDICLS = 0;
            boost::fill(nFirstWithDegree, -1);
            bool empty = true; // used to detect different connected components

            int firstVal  = reverse ? maxDegreeInCurrentLevelSet : 0;
            int finalVal  = reverse ? -1 : maxDegreeInCurrentLevelSet + 1;
            int increment = reverse ? -1 : 1;

            for(int soughtDegree = firstVal; soughtDegree != finalVal; soughtDegree += increment)
            {
                int node = firstWithDegree[soughtDegree];
                while (node > 0) {
                    // Visit neighbors
                    for(row_iterator a = backend::row_begin(A, node); a; ++a) {
                        int c = a.col();
                        if (levelSet[c] == 0) {
                            levelSet[c] = currentLevelSet + 1;
                            perm[next] = c;
                            ++next;
                            empty = false; // this level set is not empty
                            nextSameDegree[c] = nFirstWithDegree[degree[c]];
                            nFirstWithDegree[degree[c]] = c;
                            nMDICLS = std::max(nMDICLS, degree[c]);
                        }
                    }
                    node = nextSameDegree[node];
                }
            }

            ++currentLevelSet;
            maxDegreeInCurrentLevelSet = nMDICLS;
            for(int i = 0; i <= nMDICLS; ++i)
                firstWithDegree[i] = nFirstWithDegree[i];

            if (empty) {
                // The graph contains another connected component that we
                // cannot reach.  Search for a node that has not yet been
                // included in a level set, and start exploring from it.
                bool found = false;
                for(int i = 0; i < n; ++i) {
                    if (levelSet[i] == 0) {
                        perm[next] = i;
                        ++next;
                        levelSet[i] = currentLevelSet;
                        maxDegreeInCurrentLevelSet = degree[i];
                        firstWithDegree[maxDegreeInCurrentLevelSet] = i;
                        found = true;
                        break;
                    }
                }
                precondition(found, "Internal consistency error at skyline_lu");
            }
        }
    }
};

}

/// Direct solver that uses skyline LU factorization.
template <
    typename real,
    class ordering = matrix_permutation::CuthillMcKee<false>
    >
class skyline_lu {
    public:
        struct params {};

        static size_t coarse_enough() { return 50; }

        template <class Matrix>
        skyline_lu(const Matrix &A, const params &prm = params())
            : n( backend::rows(A) ), perm(n), ptr(n + 1, 0), D(n, 0), y(n)
        {
            typedef typename backend::row_iterator<Matrix>::type row_iterator;

            // Find the permutation for the ordering.
            ordering::get(A, perm);

            // Get inverse permutation
            std::vector<int> invperm(n);
            for(int i = 0; i < n; ++i) invperm[perm[i]] = i;

            /* Let us find how large the rows of L and the columns of U should
             * be.  Provisionally, we will store in ptr[i] the minimum required
             * height of column i over the diagonal, and length of row i below
             * the diagonal.  The value(i,j) in the reordered matrix will be
             * the same as the value(perm[i],perm[j]) in the original matrix;
             * or, the value(i,j) in the original matrix will be the same as
             * value(invperm[i],invperm[j]) in the reordered matrix.
             */

            // Traverse the matrix finding nonzero elements
            for(int i = 0; i < n; ++i) {
                for(row_iterator a = backend::row_begin(A, i); a; ++a) {
                    int  j = a.col();
                    real v = a.value();

                    int newi = invperm[i];
                    int newj = invperm[j];

                    if (v != 0) {
                        if (newi > newj) {
                            // row newi needs length at least newi - newj
                            if (ptr[newi] < newi - newj) ptr[newi]= newi - newj;
                        } else if (newi < newj) {
                            // column newj needs height at least newj - newi
                            if (ptr[newj] < newj - newi) ptr[newj]= newj - newi;
                        }
                    }
                }
            }

            // Transform ptr so that it doesn't contain the required lengths
            // and heights, but the indexes to the entries
            {
                int last = 0;
                for(int i = 1; i <= n; ++i) {
                    int tmp = ptr[i];
                    ptr[i] = ptr[i-1] + last;
                    last = tmp;
                }
            }

            // Allocate variables for skyline format entries
            L.resize(ptr.back(), 0);
            U.resize(ptr.back(), 0);

            // And finally traverse again the CSR matrix, copying its entries
            // into the correct places in the skyline format
            for(int i = 0; i < n; ++i) {
                for(row_iterator a = backend::row_begin(A, i); a; ++a) {
                    int  j = a.col();
                    real v = a.value();

                    int newi = invperm[i];
                    int newj = invperm[j];

                    if (v != 0) {
                        if (newi < newj) {
                            U[ ptr[newj + 1] + newi - newj ] = v;
                        } else if (newi == newj) {
                            D[newi] = v;
                        } else /* newi > newj */ {
                            L[ ptr[newi + 1] + newj - newi ] = v;
                        }
                    }
                }
            }

            factorize();
        }

        template <class Vec1, class Vec2>
        void operator()(const Vec1 &rhs, Vec2 &x) const {
            // y = L^-1 * perm[rhs] ;
            // y = U^-1 * y ;
            // x = invperm[y];

            for(int i = 0; i < n; ++i) {
                real sum = rhs[perm[i]];
                for(int k = ptr[i], j = i - ptr[i+1] + k; k < ptr[i+1]; ++k, ++j)
                    sum -= L[k] * y[j];

                y[i] = sum * D[i];
            }

            for(int j = n - 1; j >= 0; --j) {
                for(int k = ptr[j], i = j - ptr[j+1] + k; k < ptr[j+1]; ++k, ++i)
                    y[i] -= U[k] * y[j];

            }

            for(int i = 0; i < n; ++i) x[perm[i]] = y[i];
        }
    private:
        int n;
        std::vector<int> perm;
        std::vector<int> ptr;
        std::vector<real> L;
        std::vector<real> U;
        std::vector<real> D;

        mutable std::vector<real> y;

        /*
         * Perform and in-place LU factorization of a skyline matrix by Crout's
         * algorithm. The diagonal of U contains the 1's.
         * The equivalent MATLAB code for a full matrix would be:
         * for k=1:n-1
         *   A(1,k+1)=A(1,k+1)/A(1,1);
         *   for i=2:k
         *     sum=A(i,k+1);
         *       for j=1:i-1
         *         sum=sum-A(i,j)*A(j,k+1);
         *       end;
         *       A(i,k+1)=sum/A(i,i);
         *   end
         *   for i=2:k
         *     sum=A(k+1,i);
         *     for j=1:i-1
         *       sum=sum-A(j,i)*A(k+1,j);
         *     end;
         *     A(k+1,i)=sum;
         *   end
         *   sum=A(k+1,k+1);
         *   for i=1:k
         *     sum=sum-A(k+1,i)*A(i,k+1);
         *   end
         *   A(k+1,k+1)=sum;
         * end
         */
        void factorize() {
            precondition(D[0] != 0, "Zero diagonal in skyline_lu");

            for(int k = 0; k < n - 1; ++k) {
                // check whether A(1,k+1) lies within the skyline structure
                if (ptr[k + 1] + k + 1 == ptr[k + 2]) {
                    U[ptr[k+1]] = U[ptr[k+1]] / D[0];
                }

                // Compute column k+1 of U
                int indexEntry = ptr[k + 1];
                int iBeginCol  = k + 1 - ptr[k + 2] + ptr[k + 1];
                for(int i = iBeginCol; i <= k; ++indexEntry, ++i) {
                    if (i == 0) continue;

                    real sum = U[indexEntry]; // this is element U(i,k+1)

                    // Multiply row i of L and Column k+1 of U
                    int jBeginRow  = i - ptr[i + 1] + ptr[i];
                    int jBeginMult = std::max(iBeginCol, jBeginRow);

                    int indexL = ptr[i  ] + jBeginMult - jBeginRow;
                    int indexU = ptr[k+1] + jBeginMult - iBeginCol;
                    for(int j = jBeginMult; j < i; ++j, ++indexL, ++indexU)
                        sum -= L[indexL] * U[indexU];

                    U[indexEntry] = sum / D[i];
                }

                // Compute row k+1 of L
                indexEntry = ptr[k+1];
                int jBeginRow = k + 1 - ptr[k + 2] + ptr[k + 1];
                for(int i = iBeginCol; i <= k; ++indexEntry, ++i) {
                    if (i == 0) continue;

                    real sum = L[indexEntry]; // this is the element L(k+1,i)

                    // Multiply row k+1 of L and column i of U
                    int jBeginCol  = i - ptr[i+1] + ptr[i];
                    int jBeginMult = std::max(jBeginCol, jBeginRow);

                    int indexL = ptr[k+1] + jBeginMult - jBeginRow;
                    int indexU = ptr[i  ] + jBeginMult - jBeginCol;

                    for(int j = jBeginMult; j < i; ++j, ++indexL, ++indexU)
                        sum -= L[indexL] * U[indexU];

                    L[indexEntry] = sum;
                }

                // Find element in diagonal
                real sum = D[k + 1];
                for(int j = ptr[k+1]; j < ptr[k+2]; ++j)
                    sum -= L[j] * U[j];

                precondition(sum != 0, "Zero sum in skyline_lu factorization");

                D[k+1] = sum;
            }

            // Invert diagonal
            for(int i = 0; i < n; ++i) D[i] = 1 / D[i];
        }
};

} // namespace solver
} // namespace amgcl


#endif
