#pragma once
#include <grid.hpp>
#include <algorithm>
#include <cstring>
// Solves Poisson's equation: Lu = f, Lu = u_xx + u_yy

template <typename T>
void gauss_seidel(T *u, const T *f, const int n, const T h) {

        for (int i = 1; i < n - 1; ++i) {
                for (int j = 1; j < n - 1; ++j) {
                        u[j + i * n] =
                            - 0.25 * (
                                    h * h * f[j + i * n]
                                    -
                                    u[j + 1 + i * n] - u[j - 1 + i * n]
                                    -
                                    u[j + (i + 1) * n] - u[j + (i - 1) * n]);
                }
        }

}

template <typename T>
void gauss_seidel_red_black(T *u, const T *f, const int n, const T h) {

        for (int i = 1; i < n - 1; ++i) {
                for (int j = 1; j < n - 1; ++j) {
                        if ( (i + j) % 2 == 0) {
                        u[j + i * n] =
                            - 0.25 * (
                                    h * h * f[j + i * n]
                                    -
                                    u[j + 1 + i * n] - u[j - 1 + i * n]
                                    -
                                    u[j + (i + 1) * n] - u[j + (i - 1) * n]);
                        }
                }
        }

        for (int i = 1; i < n - 1; ++i) {
                for (int j = 1; j < n - 1; ++j) {
                        if ( (i + j) % 2 == 1) {
                        u[j + i * n] =
                            - 0.25 * (
                                    h * h * f[j + i * n]
                                    -
                                    u[j + 1 + i * n] - u[j - 1 + i * n]
                                    -
                                    u[j + (i + 1) * n] - u[j + (i - 1) * n]);
                        }
                }
        }
}

template <typename T>
void poisson_residual(T *r, const T *u, const T *f, const int n, const T h) {

        T hi2 = 1.0 / (h * h);
        for (int i = 1; i < n - 1; ++i) {
                for (int j = 1; j < n - 1; ++j) {
                        r[j + i * n] = 
                        f[j + i * n] - (
                                        u[j + 1 + i * n] + u[j - 1 + i * n] +
                                        - 4.0 * u[j + i * n] + u[j + (i + 1) * n] +
                                        u[j + (i - 1) * n]) * hi2;
                }
        }

}

template <typename T>
void forcing_function(T *f, const int n, const T h, const T modes=1.0) {

        T s = 2.0 * M_PI * modes / (h * (n - 1));
        memset(f, 0, n * n * sizeof(T));
        for (int i = 0; i < n; ++i) {
                for (int j = 0; j < n; ++j) {
                        f[j + n * i] = -2 * s * s * sin(s * h * i) * sin(s * h * j);
                }
        }

}

template <typename T>
void exact_solution(T *u, const int n, const T h, const T modes=1.0) {

        T s = 2.0 * M_PI * modes / (h * (n - 1));
        for (int i = 0; i < n; ++i) {
                for (int j = 0; j < n; ++j) {
                        u[j + n * i] = sin(s * h * j) * sin(s * h * i);
                }
        }
}

template <typename T>
__inline__ void base_case(T *u, const T *f, const T h) {
        u[1 + 3 * 1] = -0.5 * f[1 + 3 * 1] * h * h;
}

template <typename T, typename S>
void multigrid_v_cycle(const int l, S& smoother, T *u, T *f, T *r, T *v, T *w, const T h) {

        if (l == 1) {
                base_case(u, f, h);
                return;
        }

        int nu = (1 << l) + 1; 
        int nv = (1 << (l - 1)) + 1;
        // Get e^(l-1) and residual r^(l-1)
        T *el = &v[nv * nv];
        T *rl = &w[nv * nv];

        smoother(u, f, nu, h);

        // r^l := f - Lu^l
        poisson_residual(r, u, f, nu, h);

        // r^(l-1) := R * r 
        grid_restrict(rl, nv, nv, r, nu, nu, 0.0, 1.0);

        // Solve: A^(l-1) e^(l-1) = r^(l-1)
        multigrid_v_cycle(l - 1, smoother, el, rl, r, v, w, 2 * h); 

        // Prolongate and add correction u^l := u^l +  Pe^(l-1)
        grid_prolongate(u, nu, nu, el, nv, nv, 1.0, 1.0);

        smoother(u, f, nu, h);
}

// Size of all of the combined grids
size_t multigrid_size(const int l) {
        size_t size = 0;
        for (int i = 0; i <= l; ++i) {
                int n = (1 << i) + 1;
                size += n * n;
        }
        return size;
}

template <typename F, typename P, typename T>
class Multigrid {
        private:
                // v and w are buffers of size (l + 1) * log (l + 1), 
                // v[0] .. v[l] (one per grid)
                // v is used for the initial guess and w is used for the restricted residual
                T *v = 0, *w = 0, *r = 0;
                int l;
                size_t num_bytes = 0;
                F smoother;
        public:

                Multigrid() { }
                Multigrid(P& p) : l(p.l) {
                        num_bytes = multigrid_size(l) * sizeof(T);
                        v = (T*)malloc(num_bytes);
                        w = (T*)malloc(num_bytes);
                        int n = (1 << p.l) + 1;
                        r = (T*)malloc(sizeof(T) * n * n);
                }

                void operator()(P& p) {
                        memset(v, 0, num_bytes);
                        memset(w, 0, num_bytes);
                        multigrid_v_cycle<T, F>(l, smoother, p.u, p.f, r, v, w, p.h);
                }

                ~Multigrid(void) {
                        if (v != nullptr) free(v);
                        if (w != nullptr) free(w);
                        if (r != nullptr) free(r);
                }

                const char *name() {
                        static char name[2048];
                        sprintf(name, "Multi-Grid<%s>", smoother.name());
                        return name;
                }

};

class GaussSeidel {
        public:
                GaussSeidel() { }
        template <typename P>
                GaussSeidel(P& p) { }
        template <typename P>
        void operator()(P& p) {
                gauss_seidel(p.u, p.f, p.n, p.h);
        }

        template <typename T>
        void operator()(T *u, T *f, const int n, const T h) {
                gauss_seidel(u, f, n, h);
        }

        const char *name() {
                return "Gauss-Seidel";
        }

};

class GaussSeidelRedBlack {
        public:
                GaussSeidelRedBlack() { }
        template <typename P>
                GaussSeidelRedBlack(P& p) { }
        template <typename T>
        void operator()(T *u, const T *f, const int n, const T h) {
                gauss_seidel_red_black(u, f, n, h);
        }

        template <typename P>
        void operator()(P& p) {
                gauss_seidel_red_black(p.u, p.f, p.n, p.h);
        }
        const char *name() {
                return "Gauss-Seidel (red-black)";
        }

};

template <typename T>
class Poisson {
        public:
                int n;
                int l;
                T h;
                T modes;
                T *u, *f, *r;
                size_t num_bytes;

        Poisson(int l, T h, T modes) : l(l), h(h), modes(modes) {
                n = (1 << l) + 1;
                num_bytes = sizeof(T) * n * n;
                u = (T*)malloc(num_bytes);
                f = (T*)malloc(num_bytes);
                r = (T*)malloc(num_bytes);
                memset(u, 0, num_bytes);
                memset(f, 0, num_bytes);
                memset(r, 0, num_bytes);
                forcing_function(f, n, h, modes);
        }

        T error() {
                T *v = (T*)malloc(num_bytes);
                memset(v, 0, num_bytes);
                exact_solution(v, n, h, modes);
                grid_subtract(r, u, v, n, n);
                T err = grid_l1norm(r, n, n, h, h);
                free(v);
                return err;
        }

        void residual(void) {
                poisson_residual(r, u, f, n, h);
        }

        T norm(void) {
                return grid_l1norm(r, n, n, h, h);
        }

        ~Poisson() {
                free(u);
                free(f);
                free(r);
        }
};


