/*
 * MicroHH
 * Copyright (c) 2011-2017 Chiel van Heerwaarden
 * Copyright (c) 2011-2017 Thijs Heus
 * Copyright (c) 2014-2017 Bart van Stratum
 *
 * This file is part of MicroHH
 *
 * MicroHH is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * MicroHH is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with MicroHH.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstdio>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <math.h>

#include "master.h"
#include "grid.h"
#include "soil_grid.h"
#include "fields.h"
#include "soil_field3d.h"
#include "stats.h"
#include "cross.h"
#include "column.h"
#include "constants.h"
#include "netcdf_interface.h"
#include "constants.h"
#include "radiation.h"
#include "thermo.h"
#include "boundary.h"
#include "fast_math.h"
#include "timeloop.h"
#include "microphys.h"
#include "boundary_cyclic.h"

#include "land_surface.h"

using namespace Constants;
using namespace Fast_math;

namespace soil
{
    //
    // TO-DO: move to separate source/header file
    //

    template<typename TF>
    inline TF calc_diffusivity_vg(
            const TF vg_a, const TF vg_l, const TF vg_m, const TF gamma_sat,
            const TF theta_res, const TF theta_sat, const TF theta_norm)
    {
        const TF vg_mi = TF(1) / vg_m;

        return (TF(1) - vg_m) * gamma_sat / (vg_a * vg_m * (theta_sat - theta_res))
                    * pow(theta_norm, (vg_l - vg_mi))
                    * (pow((TF(1) - pow(theta_norm, vg_mi)), -vg_m)
                    + pow((TF(1) - pow(theta_norm, vg_mi)), vg_m) - TF(2));
    }

    template<typename TF>
    inline TF calc_conductivity_vg(
            const TF theta_norm, const TF vg_l, const TF vg_m, const TF gamma_sat)
    {
        return gamma_sat * pow(theta_norm, vg_l)
                    * pow((TF(1) - pow((TF(1) - pow(theta_norm, (1. / vg_m))), vg_m)), 2);
    }

    template<typename TF>
    void init_soil_homogeneous(
            TF* const restrict soil_fld,
            const TF* const restrict soil_prof,
            const int istart, const int iend,
            const int jstart, const int jend,
            const int kstart, const int kend,
            const int isize, const int ijsize)
    {
        for (int k=kstart; k<kend; ++k)
            for (int j=jstart; j<jend; ++j)
                #pragma ivdep
                for (int i=istart; i<iend; ++i)
                {
                    const int ijk = i+j * isize + k*ijsize;
                    soil_fld[ijk] = soil_prof[k-kstart];
                }
    }

    template<typename TF>
    void calc_soil_properties(
            TF* const restrict kappa_theta_min, TF* const restrict kappa_theta_max,
            TF* const restrict gamma_theta_min, TF* const restrict gamma_theta_max,
            TF* const restrict vg_m,
            TF* const restrict gamma_T_dry, TF* const restrict rho_C,
            const TF* const restrict vg_a,
            const TF* const restrict vg_l,
            const TF* const restrict vg_n,
            const TF* const restrict gamma_theta_sat,
            const TF* const restrict theta_res, const TF* const restrict theta_sat,
            const TF* const restrict theta_fc,
            const int table_size)
    {
        for (int i=0; i<table_size; ++i)
        {
            // van Genuchten parameter `m`
            vg_m[i] = (TF(1) - (TF(1) / vg_n[i]));

            // Min/max values diffusivity soil moisture
            const TF theta_norm_min =
                (TF(1.001) * theta_res[i] - theta_res[i]) / (theta_sat[i] - theta_res[i]);
            const TF theta_norm_max =
                (TF(0.999) * theta_sat[i] - theta_res[i]) / (theta_sat[i] - theta_res[i]);

            kappa_theta_min[i] = calc_diffusivity_vg(
                    vg_a[i], vg_l[i], vg_m[i], gamma_theta_sat[i],
                    theta_res[i], theta_sat[i], theta_norm_min);
            kappa_theta_max[i] = calc_diffusivity_vg(
                    vg_a[i], vg_l[i], vg_m[i], gamma_theta_sat[i],
                    theta_res[i], theta_sat[i], theta_norm_max);

            // Min/max values conductivity soil moisture
            gamma_theta_min[i] = TF(0);
            gamma_theta_max[i] = gamma_theta_sat[i];

            // Conductivity temperature
            const TF rho_solid = TF(2700);  // Density of dry solid soil (kg m-3); PL98, eq. 6
            const TF rho_dry = (TF(1) - theta_sat[i]) * rho_solid;  // Density of soil (kg m-3)

            gamma_T_dry[i] = (TF(0.135) * rho_dry + TF(64.7)) / (rho_solid - TF(0.947) * rho_dry);
            rho_C[i] = (TF(1) - theta_sat[i]) * Constants::rho_C_matrix<TF>
                    + theta_fc[i] * Constants::rho_C_water<TF>;
        }
    }

    template<typename TF>
    void calc_root_column(
            TF* const restrict root_frac,
            const TF* const restrict zh,
            const TF a_root, const TF b_root,
            const int kstart, const int kend)
    {
        TF root_frac_sum = TF(0);

        for (int k=kstart+1; k<kend; ++k)
        {
            root_frac[k] = 0.5 * (exp(a_root * zh[k+1]) + \
                                  exp(b_root * zh[k+1]) - \
                                  exp(a_root * zh[k  ]) - \
                                  exp(b_root * zh[k  ]));

            root_frac_sum += root_frac[k];
        }

        // Make sure the root fraction sums to one.
        root_frac[kstart] = TF(1) - root_frac_sum;
    }

    template<typename TF>
    void calc_root_fraction(
            TF* const restrict root_frac,
            const TF* const restrict a_root,
            const TF* const restrict b_root,
            const TF* const restrict zh,
            const int istart, const int iend,
            const int jstart, const int jend,
            const int kstart, const int kend,
            const int icells, const int ijcells)
    {
        for (int j=jstart; j<jend; ++j)
            for (int i=istart; i<iend; ++i)
            {
                TF root_frac_sum = TF(0);

                for (int k=kstart+1; k<kend; ++k)
                {
                    const int ij  = i + j*icells;
                    const int ijk = i + j*icells + k*ijcells;

                    root_frac[ijk] = 0.5 * (exp(a_root[ij] * zh[k+1]) + \
                                            exp(b_root[ij] * zh[k+1]) - \
                                            exp(a_root[ij] * zh[k  ]) - \
                                            exp(b_root[ij] * zh[k  ]));

                    root_frac_sum += root_frac[ijk];
                }

                const int ijk = i +j*icells + kstart*ijcells;
                // Make sure the root fraction sums to one.
                root_frac[ijk] = TF(1) - root_frac_sum;
            }
    }

    template<typename TF>
    void calc_root_weighted_mean_theta(
            TF* const restrict theta_mean,
            const TF* const restrict theta,
            const int* const restrict soil_index,
            const TF* const restrict root_fraction,
            const TF* const restrict theta_wp,
            const TF* const restrict theta_fc,
            const int istart, const int iend,
            const int jstart, const int jend,
            const int kstart, const int kend,
            const int icells, const int ijcells)
    {
        for (int j=jstart; j<jend; ++j)
            #pragma ivdep
            for (int i=istart; i<iend; ++i)
            {
                const int ij = i + j*icells;
                theta_mean[ij] = TF(0);
            }

        for (int k=kstart; k<kend; ++k)
            for (int j=jstart; j<jend; ++j)
                #pragma ivdep
                for (int i=istart; i<iend; ++i)
                {
                    const int ij  = i + j*icells;
                    const int ijk = i + j*icells + k*ijcells;
                    const int si  = soil_index[ijk];

                    const TF theta_lim = std::max(theta[ijk], theta_wp[si]);
                    theta_mean[ij] += root_fraction[ijk]
                            * (theta_lim - theta_wp[si]) / (theta_fc[si] - theta_wp[si]);
                }
    }

    template<typename TF>
    void calc_thermal_properties(
            TF* const restrict kappa,
            TF* const restrict gamma,
            const int* const restrict soil_index,
            const TF* const restrict theta,
            const TF* const restrict theta_sat,
            const TF* const restrict gamma_dry,
            const TF* const restrict rho_C,
            const int istart, const int iend,
            const int jstart, const int jend,
            const int kstart, const int kend,
            const int icells, const int ijcells)
    {
        for (int k=kstart; k<kend; ++k)
            for (int j=jstart; j<jend; ++j)
                #pragma ivdep
                for (int i=istart; i<iend; ++i)
                {
                    const int ijk = i + j*icells + k*ijcells;
                    const int si = soil_index[ijk];

                    // Heat conductivity at saturation (from IFS code..)
                    const TF lambda_T_sat = pow(Constants::gamma_T_matrix<TF>, (TF(1) - theta_sat[si]))
                                            * pow(Constants::gamma_T_water<TF>, theta[ijk])
                                            * pow(TF(2.2), (theta_sat[si] - theta[ijk]));

                    // Kersten number for fine soils [IFS eq 8.64] (-)
                    const TF kersten = log10(std::max(TF(0.1), theta[ijk] / theta_sat[si])) + TF(1);

                    // Heat conductivity soil [IFS eq 8.62] (W m-1 K-1)
                    gamma[ijk] = kersten * (lambda_T_sat - gamma_dry[si]) + gamma_dry[si];

                    // Heat diffusivity (m2 s-1)
                    kappa[ijk] = gamma[ijk] / rho_C[si];
                }
    }

    template<typename TF>
    void calc_hydraulic_properties(
            TF* const restrict kappa,
            TF* const restrict gamma,
            const int* const restrict soil_index,
            const TF* const restrict theta,
            const TF* const restrict theta_sat,
            const TF* const restrict theta_res,
            const TF* const restrict vg_a,
            const TF* const restrict vg_l,
            const TF* const restrict vg_m,
            const TF* const restrict gamma_sat,
            const TF* const restrict gamma_min,
            const TF* const restrict gamma_max,
            const TF* const restrict kappa_min,
            const TF* const restrict kappa_max,
            const int istart, const int iend,
            const int jstart, const int jend,
            const int kstart, const int kend,
            const int icells, const int ijcells)
    {
        for (int k=kstart; k<kend; ++k)
            for (int j=jstart; j<jend; ++j)
                #pragma ivdep
                for (int i=istart; i<iend; ++i)
                {
                    const int ijk = i + j*icells + k*ijcells;
                    const int si = soil_index[ijk];

                    // Limit soil moisture just above the residual soil moisture content
                    const TF theta_lim = std::max(theta[ijk], TF(1.001) * theta_res[si]);

                    // Dimensionless soil water content
                    const TF theta_norm = (theta_lim - theta_res[si]) / (theta_sat[si] - theta_res[si]);

                    // Calculate & limit the diffusivity
                    kappa[ijk] = calc_diffusivity_vg(
                            vg_a[si], vg_l[si], vg_m[si], gamma_sat[si],
                            theta_res[si], theta_sat[si], theta_norm);
                    kappa[ijk] = std::max(std::min(kappa_max[si], kappa[ijk]), kappa_min[si]);

                    // Calculate & limit the conductivity
                    gamma[ijk] = calc_conductivity_vg(
                            theta_norm, vg_l[si], vg_m[si], gamma_sat[si]);
                    gamma[ijk] = std::max(std::min(gamma_max[si], gamma[ijk]), gamma_min[si]);
                }
    }

    template<typename TF>
    void calc_root_water_extraction(
            TF* const restrict extraction,
            TF* const restrict tmp,
            const TF* const restrict theta,
            const TF* const restrict root_frac,
            const TF* const restrict LE_veg,
            const TF* const restrict dzi,
            const int istart, const int iend,
            const int jstart, const int jend,
            const int kstart, const int kend,
            const int icells, const int ijcells)
    {
        const TF fac = TF(1) / (rho_w<TF> * Lv<TF>);

        for (int j=jstart; j<jend; ++j)
            #pragma ivdep
            for (int i=istart; i<iend; ++i)
            {
                const int ij  = i + j*icells;
                tmp[ij] = TF(0);
            }

        for (int k=kstart; k<kend; ++k)
            for (int j=jstart; j<jend; ++j)
                #pragma ivdep
                for (int i=istart; i<iend; ++i)
                {
                    const int ij  = i + j*icells;
                    const int ijk = ij + k*ijcells;

                    tmp[ij] += root_frac[ijk] * theta[ijk];
                }

        for (int k=kstart; k<kend; ++k)
            for (int j=jstart; j<jend; ++j)
                #pragma ivdep
                for (int i=istart; i<iend; ++i)
                {
                    const int ij  = i + j*icells;
                    const int ijk = ij + k*ijcells;

                    const TF theta_frac = root_frac[ijk] * theta[ijk] / tmp[ij];
                    extraction[ijk] = -std::max(TF(0), LE_veg[ij]) * fac * dzi[k] * theta_frac;
                }
    }

    template<typename TF, Soil_interpolation_type interpolation_type>
    void interp_2_vertical(
            TF* const restrict fldh,
            const TF* const restrict fld,
            const int istart, const int iend,
            const int jstart, const int jend,
            const int kstart, const int kend,
            const int icells, const int ijcells)
    {
        const int kk = ijcells;

        for (int k=kstart+1; k<kend; ++k)
            for (int j=jstart; j<jend; ++j)
                #pragma ivdep
                for (int i=istart; i<iend; ++i)
                {
                    const int ijk = i + j*icells + k*ijcells;

                    if (interpolation_type == Soil_interpolation_type::Mean)
                        fldh[ijk] = TF(0.5) * (fld[ijk] + fld[ijk-kk]);
                    else if(interpolation_type == Soil_interpolation_type::Max)
                        fldh[ijk] = std::max(fld[ijk], fld[ijk-kk]);
                }
    }

    template<typename TF>
    void set_bcs_temperature(
            TF* const restrict flux_top,
            TF* const restrict flux_bot,
            const TF* const restrict G,
            const TF* const restrict rho_C,
            const int* const restrict soil_index,
            const int istart, const int iend,
            const int jstart, const int jend,
            const int kend,
            const int icells, const int ijcells)
    {
        for (int j=jstart; j<jend; ++j)
            #pragma ivdep
            for (int i=istart; i<iend; ++i)
            {
                const int ij  = i + j*icells;
                const int ijk = ij + (kend-1)*ijcells;  // Top soil layer
                const int si  = soil_index[ijk];

                flux_top[ij] = G[ij] / rho_C[si];
                flux_bot[ij] = TF(0);
            }
    }

    template<typename TF, bool sw_free_drainage>
    void set_bcs_moisture(
            TF* const restrict flux_top,
            TF* const restrict flux_bot,
            TF* const restrict conductivity_h,
            const TF* const restrict LE_soil,
            const TF* const restrict tile_frac_soil,
            const TF* const restrict throughfall,
            const int istart, const int iend,
            const int jstart, const int jend,
            const int kstart, const int kend,
            const int icells, const int ijcells)
    {
        const TF fac = TF(1) / (rho_w<TF> * Lv<TF>);

        const int kk = ijcells;
        for (int j=jstart; j<jend; ++j)
            #pragma ivdep
            for (int i=istart; i<iend; ++i)
            {
                const int ij = i + j*icells;
                flux_top[ij] = tile_frac_soil[ij] * LE_soil[ij] * fac + throughfall[ij];
                flux_bot[ij] = TF(0);

                // Set free drainage bottom BC:
                const int ijk = ij + kstart*ijcells;
                if (sw_free_drainage)
                    conductivity_h[ijk] = conductivity_h[ijk+kk];
                else
                    conductivity_h[ijk] = TF(0);
            }
    }

    template<typename TF, bool sw_source_term, bool sw_conductivity_term>
    void diff_explicit(
            TF* const restrict tend,
            const TF* const restrict fld,
            const TF* const restrict kappa_h,
            const TF* const restrict gamma_h,
            const TF* const restrict source,
            const TF* const restrict flux_top,
            const TF* const restrict flux_bot,
            const TF* const restrict dzi,
            const TF* const restrict dzhi,
            const int istart, const int iend,
            const int jstart, const int jend,
            const int kstart, const int kend,
            const int icells, const int ijcells)
    {
        const int kk = ijcells;
        int k;

        // Bottom soil level
        k = kstart;
        for (int j=jstart; j<jend; ++j)
            #pragma ivdep
            for (int i=istart; i<iend; ++i)
            {
                const int ij  = i + j*icells;
                const int ijk = ij + k*ijcells;

                tend[ijk] += ((kappa_h[ijk+kk] * (fld[ijk+kk] - fld[ijk]) * dzhi[k+1]) + flux_bot[ij])*dzi[k];

                if (sw_conductivity_term)
                    tend[ijk] += (gamma_h[ijk+kk] - gamma_h[ijk]) * dzi[k];
                if (sw_source_term)
                    tend[ijk] += source[ijk];
            }

        // Top soil level
        k = kend-1;
        for (int j=jstart; j<jend; ++j)
            #pragma ivdep
            for (int i=istart; i<iend; ++i)
            {
                const int ij  = i + j*icells;
                const int ijk = ij + k*ijcells;

                tend[ijk] += (-flux_top[ij] - (kappa_h[ijk] * (fld[ijk] - fld[ijk-kk]) * dzhi[k]))*dzi[k];

                if (sw_conductivity_term)
                    tend[ijk] -= gamma_h[ijk] * dzi[k];
                if (sw_source_term)
                    tend[ijk] += source[ijk];
            }

        // Interior
        for (int k=kstart+1; k<kend-1; ++k)
            for (int j=jstart; j<jend; ++j)
                #pragma ivdep
                for (int i=istart; i<iend; ++i)
                {
                    const int ijk = i + j*icells + k*ijcells;

                    tend[ijk] += ((kappa_h[ijk+kk] * (fld[ijk+kk] - fld[ijk   ]) * dzhi[k+1])
                               - (kappa_h[ijk   ] * (fld[ijk   ] - fld[ijk-kk]) * dzhi[k  ])) * dzi[k];

                    if (sw_conductivity_term)
                        tend[ijk] += (gamma_h[ijk+kk] - gamma_h[ijk]) * dzi[k];
                    if (sw_source_term)
                        tend[ijk] += source[ijk];
                }
    }
}

namespace lsm
{
    //
    // TO-DO: move to separate source/header file
    //

    template<typename TF>
    void init_tile(Surface_tile<TF>& tile, const int ijcells)
    {
        tile.fraction.resize(ijcells);

        tile.rs.resize(ijcells);

        tile.H.resize(ijcells);
        tile.LE.resize(ijcells);
        tile.G.resize(ijcells);

        tile.T_bot.resize(ijcells);
        tile.thl_bot.resize(ijcells);
        tile.qt_bot.resize(ijcells);

        tile.thl_fluxbot.resize(ijcells);
        tile.qt_fluxbot.resize(ijcells);
    }

    template<typename TF>
    void calc_tile_fractions(
            TF* const restrict tile_frac_veg,
            TF* const restrict tile_frac_soil,
            TF* const restrict tile_frac_wet,
            const TF* const restrict wl,
            const TF* const restrict c_veg,
            const TF* const restrict lai,
            const int istart, const int iend,
            const int jstart, const int jend,
            const int icells)
    {
        for (int j=jstart; j<jend; ++j)
            #pragma ivdep
            for (int i=istart; i<iend; ++i)
            {
                const int ij  = i + j*icells;

                const TF wlm = Constants::wlmax<TF> * (TF(1)-c_veg[ij] + c_veg[ij]*lai[ij]);

                tile_frac_wet[ij]  = std::min(TF(1), wl[ij]/wlm);
                tile_frac_veg[ij]  = (TF(1)-tile_frac_wet[ij]) * c_veg[ij];
                tile_frac_soil[ij] = (TF(1)-tile_frac_wet[ij]) * (TF(1)-c_veg[ij]);
            }
    }

    template<typename TF>
    void calc_liquid_water_reservoir(
            TF* const restrict wl_tend,
            TF* const restrict interception,
            TF* const restrict throughfall,
            const TF* const restrict wl,
            const TF* const restrict LE_veg,
            const TF* const restrict LE_soil,
            const TF* const restrict LE_wet,
            const TF* const restrict tile_frac_veg,
            const TF* const restrict tile_frac_soil,
            const TF* const restrict tile_frac_wet,
            const TF* const restrict rain_rate,
            const TF* const restrict c_veg,
            const TF* const restrict lai,
            const double subdt,
            const int istart, const int iend,
            const int jstart, const int jend,
            const int icells)
    {
        const TF intercept_eff = TF(0.5);
        const TF to_ms  = TF(1) / (Constants::rho_w<TF> * Constants::Lv<TF>);
        const TF subdti = TF(1) / subdt;

        for (int j=jstart; j<jend; ++j)
            #pragma ivdep
            for (int i=istart; i<iend; ++i)
            {
                const int ij  = i + j*icells;

                // Max `wl` accounting for vegetation fraction and LAI
                const TF wlm = Constants::wlmax<TF> * (TF(1) - c_veg[ij] + c_veg[ij] * lai[ij]);

                // Max and min possible tendencies
                const TF wl_tend_max = (wlm - wl[ij]) * subdti - wl_tend[ij];
                const TF wl_tend_min = (    - wl[ij]) * subdti - wl_tend[ij];

                // Tendency due to evaporation from liquid water reservoir/tile.
                const TF wl_tend_liq = -std::max(TF(0), tile_frac_wet[ij] * LE_wet[ij] * to_ms);

                // Tendency due to dewfall into vegetation/soil/liquid water tiles
                const TF wl_tend_dew = -( std::min(TF(0), tile_frac_wet[ij]  * LE_wet[ij]  * to_ms)
                                        + std::min(TF(0), tile_frac_veg[ij]  * LE_veg[ij]  * to_ms)
                                        + std::min(TF(0), tile_frac_soil[ij] * LE_soil[ij] * to_ms) );

                // Tendency due to interception of precipitation by vegetation
                // Rain rate is positive downwards, so minus is excluded.
                const TF wl_tend_precip = intercept_eff * c_veg[ij] * rain_rate[ij];

                // Total and limited tendencies
                const TF wl_tend_sum = wl_tend_liq + wl_tend_dew + wl_tend_precip;
                const TF wl_tend_lim = std::min(wl_tend_max, std::max(wl_tend_min,  wl_tend_sum));

                // Diagnose throughfall and interception
                throughfall[ij] =
                    -(TF(1)-c_veg[ij]) * rain_rate[ij]
                    -(TF(1)-intercept_eff) * c_veg[ij] * rain_rate[ij] +
                    std::min(TF(0), wl_tend_lim - wl_tend_sum);

                interception[ij] = std::max(TF(0), wl_tend_lim);

                wl_tend[ij] += wl_tend_lim;
            }
    }

    template<typename TF>
    void calc_resistance_functions(
            TF* const restrict f1,
            TF* const restrict f2,
            TF* const restrict f2b,
            TF* const restrict f3,
            const TF* const restrict sw_dn,
            const TF* const restrict theta,
            const TF* const restrict theta_mean_n,
            const TF* const restrict vpd,
            const TF* const restrict gD,
            const TF* const restrict c_veg,
            const TF* const restrict theta_wp,
            const TF* const restrict theta_fc,
            const TF* const restrict theta_res,
            const int* const restrict soil_index,
            const int istart, const int iend,
            const int jstart, const int jend,
            const int kend,
            const int icells, const int ijcells)
    {
        // Constants f1 calculation:
        const TF a_f1 = 0.81;
        const TF b_f1 = 0.004;
        const TF c_f1 = 0.05;

        for (int j=jstart; j<jend; ++j)
            #pragma ivdep
            for (int i=istart; i<iend; ++i)
            {
                const int ij  = i + j*icells;
                const int ijk = i + j*icells + (kend-1)*ijcells;    // Top soil layer
                const int si  = soil_index[ij];

                // f1: reduction vegetation resistance as f(sw_in):
                const TF sw_dn_lim = std::max(TF(0), sw_dn[ij]);
                f1[ij] = TF(1)/std::min( TF(1), (b_f1*sw_dn_lim + c_f1) / (a_f1 * (b_f1*sw_dn_lim + TF(1))) );

                // f2: reduction vegetation resistance as f(theta):
                f2[ij] = TF(1)/std::min( TF(1), std::max(TF(1e-9), theta_mean_n[ij]) );

                // f3: reduction vegetation resistance as f(VPD):
                f3[ij] = TF(1)/exp(-gD[ij] * vpd[ij]);

                // f2b: reduction soil resistance as f(theta)
                const TF theta_min = c_veg[ij] * theta_wp[si] + (TF(1)-c_veg[ij]) * theta_res[si];
                const TF theta_rel = (theta[ijk] - theta_min) / (theta_fc[si] - theta_min);
                f2b[ij] = TF(1)/std::min(TF(1), std::max(TF(1e-9), theta_rel));
            }
    }

    template<typename TF>
    void calc_canopy_resistance(
            TF* const restrict rs,
            const TF* const restrict rs_min,
            const TF* const restrict lai,
            const TF* const restrict f1,
            const TF* const restrict f2,
            const TF* const restrict f3,
            const int istart, const int iend,
            const int jstart, const int jend,
            const int icells)
    {
        for (int j=jstart; j<jend; ++j)
            #pragma ivdep
            for (int i=istart; i<iend; ++i)
            {
                const int ij  = i + j*icells;
                rs[ij] = rs_min[ij] / lai[ij] * f1[ij] * f2[ij] * f3[ij];
            }
    }

    template<typename TF>
    void calc_soil_resistance(
            TF* const restrict rs,
            const TF* const restrict rs_min,
            const TF* const restrict f2b,
            const int istart, const int iend,
            const int jstart, const int jend,
            const int icells)
    {
        for (int j=jstart; j<jend; ++j)
            #pragma ivdep
            for (int i=istart; i<iend; ++i)
            {
                const int ij  = i + j*icells;
                rs[ij] = rs_min[ij] * f2b[ij];
            }
    }

    template<typename TF>
    void calc_fluxes(
            TF* const restrict H,
            TF* const restrict LE,
            TF* const restrict G,
            const TF* const restrict T,
            const TF* const restrict qt,
            const TF* const restrict T_soil,
            const TF* const restrict T_bot,
            const TF* const restrict qsat_bot,
            const TF* const restrict dqsatdT_bot,
            const TF* const restrict ra,
            const TF* const restrict rs,
            const TF* const restrict lambda,
            const TF* const restrict sw_dn,
            const TF* const restrict sw_up,
            const TF* const restrict lw_dn,
            const TF* const restrict lw_up,
            const TF* const restrict rhorefh,
            const int istart, const int iend,
            const int jstart, const int jend,
            const int kstart, const int kend_soil,
            const int icells, const int ijcells)
    {
        for (int j=jstart; j<jend; ++j)
            #pragma ivdep
            for (int i=istart; i<iend; ++i)
            {
                const int ij    = i + j*icells;
                const int ijk   = ij + kstart*ijcells;
                const int ijk_s = ij + (kend_soil-1)*ijcells;

                // Disable canopy resistance in case of dew fall
                const TF rs_lim = qsat_bot[ij] < qt[ijk] ? TF(0) : rs[ij];

                // Recuring factors
                const TF fH  = rhorefh[kstart] * cp<TF> / ra[ij];
                const TF fLE = rhorefh[kstart] * Lv<TF> / (ra[ij] + rs_lim);
                const TF fG  = lambda[ij];

                // Net radiation; negative sign = net input of energy at surface
                const TF Qnet = -(sw_dn[ij] - sw_up[ij] + lw_dn[ij] - lw_up[ij]);

                // Solve for the new surface temperature
                const TF num = -(Qnet - lw_up[ij]
                        - fH * T[ij] + (qsat_bot[ij] - dqsatdT_bot[ij] * T_bot[ij] - qt[ijk]) * fLE
                        - fG * T_soil[ijk_s] - TF(3) * sigma_b<TF> * pow4(T_bot[ij]));
                const TF denom = (fH + fLE * dqsatdT_bot[ij] + fG + TF(4) * sigma_b<TF> * pow3(T_bot[ij]));
                const TF T_bot_new = num / denom;

                // Update qsat with linearised relation, to make sure that the SEB closes
                const TF qsat_new = qsat_bot[ij] + dqsatdT_bot[ij] * (T_bot_new - T_bot[ij]);

                // Calculate surface fluxes
                H [ij] = fH  * (T_bot_new - T[ij]);
                LE[ij] = fLE * (qsat_new - qt[ijk]);
                G [ij] = fG  * (T_soil[ijk_s] - T_bot_new);
            }
    }

    template<typename TF>
    void calc_bcs(
            TF* const restrict thl_bot,
            TF* const restrict qt_bot,
            const TF* const restrict thl,
            const TF* const restrict qt,
            const TF* const restrict H_veg,
            const TF* const restrict H_soil,
            const TF* const restrict H_wet,
            const TF* const restrict LE_veg,
            const TF* const restrict LE_soil,
            const TF* const restrict LE_wet,
            const TF* const restrict tile_frac_veg,
            const TF* const restrict tile_frac_soil,
            const TF* const restrict tile_frac_wet,
            const TF* const restrict ra,
            const TF* const restrict rhorefh,
            const int istart, const int iend,
            const int jstart, const int jend,
            const int kstart,
            const int icells, const int ijcells)
    {
        const TF rhocp_i = TF(1) / (rhorefh[kstart] * cp<TF>);
        const TF rholv_i = TF(1) / (rhorefh[kstart] * Lv<TF>);

        for (int j=jstart; j<jend; ++j)
            #pragma ivdep
            for (int i=istart; i<iend; ++i)
            {
                const int ij  = i + j*icells;
                const int ijk = ij + kstart*ijcells;

                // Tile averaged surface fluxes
                const TF wthl = (
                    tile_frac_veg [ij] * H_veg [ij] +
                    tile_frac_soil[ij] * H_soil[ij] +
                    tile_frac_wet [ij] * H_wet [ij] ) * rhocp_i;

                const TF wqt = (
                    tile_frac_veg [ij] * LE_veg [ij] +
                    tile_frac_soil[ij] * LE_soil[ij] +
                    tile_frac_wet [ij] * LE_wet [ij] ) * rholv_i;

                // Calculate surface values
                thl_bot[ij] = thl[ijk] + wthl * ra[ij];
                qt_bot [ij] = qt[ijk]  + wqt  * ra[ij];
            }
    }

    template<typename TF>
    void calc_tiled_mean(
            TF* const restrict fld_mean,
            const TF* const restrict fld_veg,
            const TF* const restrict fld_soil,
            const TF* const restrict fld_wet,
            const TF* const restrict tile_frac_veg,
            const TF* const restrict tile_frac_soil,
            const TF* const restrict tile_frac_wet,
            const int istart, const int iend,
            const int jstart, const int jend,
            const int icells)
    {
        for (int j=jstart; j<jend; ++j)
            #pragma ivdep
            for (int i=istart; i<iend; ++i)
            {
                const int ij  = i + j*icells;

                fld_mean[ij] =
                    tile_frac_veg [ij] * fld_veg [ij] +
                    tile_frac_soil[ij] * fld_soil[ij] +
                    tile_frac_wet [ij] * fld_wet [ij];
            }
    }

    template<typename TF>
    void scale_tile_with_fraction(
            TF* const restrict fld_scaled,
            const TF* const restrict fld,
            const TF* const restrict tile_frac,
            const int istart, const int iend,
            const int jstart, const int jend,
            const int icells)
    {
        for (int j=jstart; j<jend; ++j)
            #pragma ivdep
            for (int i=istart; i<iend; ++i)
            {
                const int ij  = i + j*icells;
                fld_scaled[ij] = fld[ij] * tile_frac[ij];
            }
    }
}

template<typename TF>
Land_surface<TF>::Land_surface(
        Master& masterin, Grid<TF>& gridin, Soil_grid<TF>& soilgridin,
        Fields<TF>& fieldsin, Input& inputin) :
    master(masterin), grid(gridin), soil_grid(soilgridin),
    fields(fieldsin), boundary_cyclic(master, grid)
{
    sw_land_surface = inputin.get_item<bool>("land_surface", "sw_land_surface", "", false);

    if (sw_land_surface)
    {
        sw_homogeneous   = inputin.get_item<bool>("land_surface", "sw_homogeneous", "", true);
        sw_free_drainage = inputin.get_item<bool>("land_surface", "sw_free_drainage", "", true);

        // Create soil fields (temperature and volumetric water content)
        fields.init_prognostic_soil_field("t",     "Soil temperature", "K");
        fields.init_prognostic_soil_field("theta", "Soil volumetric water content", "m3 m-3");

        // Create prognostic 2D field for liquid water on land-surface
        fields.init_prognostic_2d_field("wl");

        // Create the land-surface tiles
        tiles.emplace("veg",  Surface_tile<TF>{});
        tiles.emplace("soil", Surface_tile<TF>{});
        tiles.emplace("wet",  Surface_tile<TF>{});

        // Open NetCDF file with soil lookup table
        nc_lookup_table =
            std::make_shared<Netcdf_file>(master, "van_genuchten_parameters.nc", Netcdf_mode::Read);
    }
}

template<typename TF>
Land_surface<TF>::~Land_surface()
{
}

template<typename TF>
void Land_surface<TF>::init()
{
    /*
       Allocate/resize the land-surface/soil fields, properties, and grid definition.
    */
    if (!sw_land_surface)
        return;

    auto& gd  = grid.get_grid_data();
    auto& sgd = soil_grid.get_grid_data();

    // Allocate the surface tiles
    for (auto& tile : tiles)
        lsm::init_tile(tile.second, gd.ijcells);
    tiles.at("veg" ).long_name = "vegetation";
    tiles.at("soil").long_name = "bare soil";
    tiles.at("wet" ).long_name = "wet skin";

    gD_coeff.resize(gd.ijcells);
    c_veg.resize(gd.ijcells);
    lai.resize(gd.ijcells);
    rs_veg_min.resize(gd.ijcells);
    rs_soil_min.resize(gd.ijcells);
    lambda.resize(gd.ijcells);

    interception.resize(gd.ijcells);
    throughfall.resize(gd.ijcells);

    // Resize the vectors which contain the soil properties
    soil_index.resize(sgd.ncells);

    diffusivity.resize   (sgd.ncells);
    diffusivity_h.resize (sgd.ncellsh);
    conductivity.resize  (sgd.ncells);
    conductivity_h.resize(sgd.ncellsh);
    source.resize        (sgd.ncells);

    root_fraction.resize(sgd.ncells);

    // Resize the lookup table
    lookup_table_size = nc_lookup_table->get_dimension_size("index");

    theta_res.resize(lookup_table_size);
    theta_wp.resize(lookup_table_size);
    theta_fc.resize(lookup_table_size);
    theta_sat.resize(lookup_table_size);

    gamma_theta_sat.resize(lookup_table_size);
    vg_a.resize(lookup_table_size);
    vg_l.resize(lookup_table_size);
    vg_n.resize(lookup_table_size);

    vg_m.resize(lookup_table_size);
    kappa_theta_max.resize(lookup_table_size);
    kappa_theta_min.resize(lookup_table_size);
    gamma_theta_max.resize(lookup_table_size);
    gamma_theta_min.resize(lookup_table_size);

    gamma_T_dry.resize(lookup_table_size);
    rho_C.resize(lookup_table_size);

    // Initialize the boundary cyclic.
    boundary_cyclic.init();
}

template<typename TF>
void Land_surface<TF>::create_cold_start(Input& input, Netcdf_handle& input_nc)
{
    /*
       Create the prognostic soil fields, initialised either
       homogeneous from the input NetCDF file, or heterogeneous
       from "other" (yet to be defined..) sources.
       This routine is only called in the `init` phase of the model (from model.cxx),
       in the `run` phase these fields are read from the restart files.
     */
    if (!sw_land_surface)
        return;

    auto& agd = grid.get_grid_data();
    auto& sgd = soil_grid.get_grid_data();

    Netcdf_group& soil_group = input_nc.get_group("soil");
    Netcdf_group& init_group = input_nc.get_group("init");

    // Init the soil variables
    if (sw_homogeneous)
    {
        // Read initial profiles from input NetCDF file
        std::vector<TF> t_prof(sgd.ktot);
        std::vector<TF> theta_prof(sgd.ktot);

        soil_group.get_variable(t_prof, "t_soil", {0}, {sgd.ktot});
        soil_group.get_variable(theta_prof, "theta_soil", {0}, {sgd.ktot});

        // Initialise soil as spatially homogeneous
        soil::init_soil_homogeneous(
                fields.sps.at("t")->fld.data(), t_prof.data(),
                agd.istart, agd.iend,
                agd.jstart, agd.jend,
                sgd.kstart, sgd.kend,
                agd.icells, agd.ijcells);

        soil::init_soil_homogeneous(
                fields.sps.at("theta")->fld.data(), theta_prof.data(),
                agd.istart, agd.iend,
                agd.jstart, agd.jend,
                sgd.kstart, sgd.kend,
                agd.icells, agd.ijcells);
    }

    // Initialise the prognostic surface variables, and/or
    // variables which are needed for consistent restarts.
    std::fill(fields.ap2d.at("wl").begin(), fields.ap2d.at("wl").end(), TF(0));

    // Set initial surface potential temperature and humidity to the atmospheric values (...)
    std::vector<TF> thl_1(1);
    std::vector<TF> qt_1(1);

    init_group.get_variable(thl_1, "thl", {0}, {1});
    init_group.get_variable(qt_1,  "qt",  {0}, {1});

    std::fill(
            fields.sp.at("thl")->fld_bot.begin(),
            fields.sp.at("thl")->fld_bot.end(), thl_1[0]);
    std::fill(
            fields.sp.at("qt")->fld_bot.begin(),
            fields.sp.at("qt")->fld_bot.end(), qt_1[0]);
}

template<typename TF>
void Land_surface<TF>::create_fields_grid_stats(
        Input& input, Netcdf_handle& input_nc,
        Stats<TF>& stats, Cross<TF>& cross, Column<TF>& column)
{
    /*
       Create/set the non-prognostic fields (soil type, ...) from the input files,
       calculate/define the soil grid, and init the soil statistics and cross-sections.
    */
    if (!sw_land_surface)
        return;

    auto& agd = grid.get_grid_data();
    auto& sgd = soil_grid.get_grid_data();

    // Init soil properties
    if (sw_homogeneous)
    {
        Netcdf_group& soil_group = input_nc.get_group("soil");

        // Soil index
        std::vector<int> soil_index_prof(sgd.ktot);
        soil_group.get_variable<int>(soil_index_prof, "index_soil", {0}, {sgd.ktot});

        soil::init_soil_homogeneous<int>(
                soil_index.data(), soil_index_prof.data(),
                agd.istart, agd.iend,
                agd.jstart, agd.jend,
                sgd.kstart, sgd.kend,
                agd.icells, agd.ijcells);

        // Root fraction
        std::vector<TF> root_frac_prof(sgd.ktot);
        soil_group.get_variable<TF>(root_frac_prof, "root_frac", {0}, {sgd.ktot});

        soil::init_soil_homogeneous<TF>(
                root_fraction.data(), root_frac_prof.data(),
                agd.istart, agd.iend,
                agd.jstart, agd.jend,
                sgd.kstart, sgd.kend,
                agd.icells, agd.ijcells);

        auto init_homogeneous = [&](std::vector<TF>& field, std::string name)
        {
            const TF value = input.get_item<TF>("land_surface", name.c_str(), "");
            std::fill(field.begin(), field.begin()+agd.ijcells, value);
        };

        // Land-surface properties
        init_homogeneous(gD_coeff, "gD");
        init_homogeneous(c_veg, "c_veg");
        init_homogeneous(lai, "lai");
        init_homogeneous(rs_veg_min, "rs_veg_min");
        init_homogeneous(rs_soil_min, "rs_soil_min");
        init_homogeneous(lambda, "lambda");
    }

    // Set the canopy resistance of the liquid water tile at zero
    std::fill(tiles.at("wet").rs.begin(), tiles.at("wet").rs.begin()+agd.ijcells, 0.);

    // Read lookup table soil
    nc_lookup_table->get_variable<TF>(theta_res, "theta_res", {0}, {lookup_table_size});
    nc_lookup_table->get_variable<TF>(theta_wp,  "theta_wp",  {0}, {lookup_table_size});
    nc_lookup_table->get_variable<TF>(theta_fc,  "theta_fc",  {0}, {lookup_table_size});
    nc_lookup_table->get_variable<TF>(theta_sat, "theta_sat", {0}, {lookup_table_size});

    nc_lookup_table->get_variable<TF>(gamma_theta_sat, "gamma_sat", {0}, {lookup_table_size});

    nc_lookup_table->get_variable<TF>(vg_a, "alpha", {0}, {lookup_table_size});
    nc_lookup_table->get_variable<TF>(vg_l, "l",     {0}, {lookup_table_size});
    nc_lookup_table->get_variable<TF>(vg_n, "n",     {0}, {lookup_table_size});

    // Calculate derived properties of the lookup table
    soil::calc_soil_properties(
            kappa_theta_min.data(), kappa_theta_max.data(),
            gamma_theta_min.data(), gamma_theta_max.data(), vg_m.data(),
            gamma_T_dry.data(), rho_C.data(),
            vg_a.data(), vg_l.data(), vg_n.data(), gamma_theta_sat.data(),
            theta_res.data(), theta_sat.data(), theta_fc.data(), lookup_table_size);

    // Init the soil statistics
    if (stats.get_switch())
    {
        std::string group_name = "land_surface";

        // Add soil dimensions to each of the statistics masks
        auto& masks = stats.get_masks();
        for (auto& mask : masks)
        {
            auto& m = mask.second;

            // Add dimensions to NetCDF file
            m.data_file->add_dimension("zs",  sgd.ktot);
            m.data_file->add_dimension("zsh", sgd.ktot+1);

            // Write the attributes
            Netcdf_variable<TF> zs_var = m.data_file->template add_variable<TF>("zs", {"zs"});
            zs_var.add_attribute("units", "m");
            zs_var.add_attribute("long_name", "Full level soil height");

            Netcdf_variable<TF> zsh_var = m.data_file->template add_variable<TF>("zsh", {"zsh"});
            zsh_var.add_attribute("units", "m");
            zsh_var.add_attribute("long_name", "Half level soil height");

            // Write the grid levels
            zs_var .insert(sgd.z,  {0});
            zsh_var.insert(sgd.zh, {0});

            m.data_file->sync();
        }

        // Add the statistics variables
        stats.add_prof("t", "Soil temperature", "K", "zs", group_name);
        stats.add_prof("theta", "Soil volumetric water content", "-", "zs", group_name);

        // Non-tiled variables
        stats.add_time_series("wl", "Liquid water reservoir", "m", group_name);
        stats.add_time_series("H",  "Sensible heat flux", "W m-2", group_name);
        stats.add_time_series("LE", "Latent heat flux", "W m-2", group_name);
        stats.add_time_series("G",  "Soil heat flux", "W m-2", group_name);

        // Tiled variables
        std::string name;
        std::string desc;
        for (auto& tile : tiles)
        {
            name = "c_" + tile.first;
            desc = "Tile fraction " + tile.second.long_name;
            stats.add_time_series(name, desc, "-", group_name);

            name = "H_" + tile.first;
            desc = "Sensible heat flux " + tile.second.long_name;
            stats.add_time_series(name, desc, "W m-2", group_name);

            name = "LE_" + tile.first;
            desc = "Latent heat flux " + tile.second.long_name;
            stats.add_time_series(name, desc, "W m-2", group_name);

            name = "G_" + tile.first;
            desc = "Soil heat flux " + tile.second.long_name;
            stats.add_time_series(name, desc, "W m-2", group_name);

            name = "rs_" + tile.first;
            desc = "Surface resistance " + tile.second.long_name;
            stats.add_time_series(name, desc, "s m-1", group_name);
        }
    }

    // Init the column statistics time series
    if (column.get_switch())
    {
        column.add_time_series("wl", "Liquid water reservoir", "m");
        column.add_time_series("H",  "Sensible heat flux", "W m-2");
        column.add_time_series("LE", "Latent heat flux", "W m-2");
        column.add_time_series("G",  "Soil heat flux", "W m-2");
    }

    // Init the soil cross-sections
    if (cross.get_switch())
    {
        std::vector<std::string> allowed_crossvars = {"t_soil", "theta_soil"};
        crosslist = cross.get_enabled_variables(allowed_crossvars);
    }
}

template<typename TF>
void Land_surface<TF>::exec_soil()
{
    if (!sw_land_surface)
        return;

    auto& agd = grid.get_grid_data();
    auto& sgd = soil_grid.get_grid_data();

    auto tmp1 = fields.get_tmp();

    // Only soil moisture has a source and conductivity term
    const bool sw_source_term_t = false;
    const bool sw_conductivity_term_t = false;
    const bool sw_source_term_theta = true;
    const bool sw_conductivity_term_theta = true;

    //
    // Soil temperature
    //
    // Calculate the thermal diffusivity at full levels
    soil::calc_thermal_properties(
            diffusivity.data(),
            conductivity.data(),
            soil_index.data(),
            fields.sps.at("theta")->fld.data(),
            theta_sat.data(),
            gamma_T_dry.data(),
            rho_C.data(),
            agd.istart, agd.iend,
            agd.jstart, agd.jend,
            sgd.kstart, sgd.kend,
            agd.icells, agd.ijcells);

    // Linear interpolation diffusivity to half levels
    soil::interp_2_vertical<TF, Soil_interpolation_type::Mean>(
            diffusivity_h.data(),
            diffusivity.data(),
            agd.istart, agd.iend,
            agd.jstart, agd.jend,
            sgd.kstart, sgd.kend,
            agd.icells, agd.ijcells);

    // Set flux boundary conditions at top and bottom of soil column
    // Top = soil heat flux (G) averaged over all tiles, bottom = zero flux.
    get_tiled_mean(tmp1->fld_bot, "G");

    soil::set_bcs_temperature(
            fields.sps.at("t")->flux_top.data(),
            fields.sps.at("t")->flux_bot.data(),
            tmp1->fld_bot.data(),
            rho_C.data(),
            soil_index.data(),
            agd.istart, agd.iend,
            agd.jstart, agd.jend,
            sgd.kend,
            agd.icells, agd.ijcells);

    // Calculate diffusive tendency
    soil::diff_explicit<TF, sw_source_term_t, sw_conductivity_term_t>(
            fields.sts.at("t")->fld.data(),
            fields.sps.at("t")->fld.data(),
            diffusivity_h.data(),
            conductivity_h.data(),
            source.data(),
            fields.sps.at("t")->flux_top.data(),
            fields.sps.at("t")->flux_bot.data(),
            sgd.dzi.data(), sgd.dzhi.data(),
            agd.istart, agd.iend,
            agd.jstart, agd.jend,
            sgd.kstart, sgd.kend,
            agd.icells, agd.ijcells);

    //
    // Soil moisture
    //
    // Calculate the hydraulic diffusivity and conductivity at full levels
    soil::calc_hydraulic_properties(
            diffusivity.data(),
            conductivity.data(),
            soil_index.data(),
            fields.sps.at("theta")->fld.data(),
            theta_sat.data(),
            theta_res.data(),
            vg_a.data(),
            vg_l.data(),
            vg_m.data(),
            gamma_theta_sat.data(),
            gamma_theta_min.data(),
            gamma_theta_max.data(),
            kappa_theta_min.data(),
            kappa_theta_max.data(),
            agd.istart, agd.iend,
            agd.jstart, agd.jend,
            sgd.kstart, sgd.kend,
            agd.icells, agd.ijcells);

    // Interpolation diffusivity and conductivity to half levels,
    // using the IFS method, which uses the max value from the
    // two surrounding grid points.
    soil::interp_2_vertical<TF, Soil_interpolation_type::Max>(
            diffusivity_h.data(),
            diffusivity.data(),
            agd.istart, agd.iend,
            agd.jstart, agd.jend,
            sgd.kstart, sgd.kend,
            agd.icells, agd.ijcells);

    soil::interp_2_vertical<TF, Soil_interpolation_type::Max>(
            conductivity_h.data(),
            conductivity.data(),
            agd.istart, agd.iend,
            agd.jstart, agd.jend,
            sgd.kstart, sgd.kend,
            agd.icells, agd.ijcells);

    // Set the boundary conditions.
    // Top = evaporation from bare soil tile.
    // Bottom = optionally free drainage (or else closed)
    if (sw_free_drainage)
        soil::set_bcs_moisture<TF, true>(
                fields.sps.at("theta")->flux_top.data(),
                fields.sps.at("theta")->flux_bot.data(),
                conductivity_h.data(),
                tiles.at("soil").LE.data(),
                tiles.at("soil").fraction.data(),
                throughfall.data(),
                agd.istart, agd.iend,
                agd.jstart, agd.jend,
                sgd.kstart, sgd.kend,
                agd.icells, agd.ijcells);
    else
        soil::set_bcs_moisture<TF, false>(
                fields.sps.at("theta")->flux_top.data(),
                fields.sps.at("theta")->flux_bot.data(),
                conductivity_h.data(),
                tiles.at("soil").LE.data(),
                tiles.at("soil").fraction.data(),
                throughfall.data(),
                agd.istart, agd.iend,
                agd.jstart, agd.jend,
                sgd.kstart, sgd.kend,
                agd.icells, agd.ijcells);

    // Calculate root water extraction
    lsm::scale_tile_with_fraction(
            tmp1->fld_bot.data(),
            tiles.at("veg").LE.data(),
            tiles.at("veg").fraction.data(),
            agd.istart, agd.iend,
            agd.jstart, agd.jend,
            agd.icells);

    soil::calc_root_water_extraction(
            source.data(),
            tmp1->fld_top.data(),   // tmp field
            fields.sps.at("theta")->fld.data(),
            root_fraction.data(),
            tmp1->fld_bot.data(),
            sgd.dzi.data(),
            agd.istart, agd.iend,
            agd.jstart, agd.jend,
            sgd.kstart, sgd.kend,
            agd.icells, agd.ijcells);

    // Calculate diffusive tendency
    soil::diff_explicit<TF, sw_source_term_theta, sw_conductivity_term_theta>(
            fields.sts.at("theta")->fld.data(),
            fields.sps.at("theta")->fld.data(),
            diffusivity_h.data(),
            conductivity_h.data(),
            source.data(),
            fields.sps.at("theta")->flux_top.data(),
            fields.sps.at("theta")->flux_bot.data(),
            sgd.dzi.data(), sgd.dzhi.data(),
            agd.istart, agd.iend,
            agd.jstart, agd.jend,
            sgd.kstart, sgd.kend,
            agd.icells, agd.ijcells);

    fields.release_tmp(tmp1);
}


template<typename TF>
void Land_surface<TF>::exec_surface(
        Radiation<TF>& radiation, Thermo<TF>& thermo, Microphys<TF>& microphys,
        Boundary<TF>& boundary, Timeloop<TF>& timeloop)
{
    if (!sw_land_surface)
        return;

    auto& agd = grid.get_grid_data();
    auto& sgd = soil_grid.get_grid_data();

    // Get references to surface radiation fluxes
    std::vector<TF>& sw_dn = radiation.get_surface_radiation("sw_down");
    std::vector<TF>& sw_up = radiation.get_surface_radiation("sw_up");
    std::vector<TF>& lw_dn = radiation.get_surface_radiation("lw_down");
    std::vector<TF>& lw_up = radiation.get_surface_radiation("lw_up");

    // Get 2D slices from 3D tmp field
    // TO-DO: add check for sufficient vertical levels in tmp field....
    auto tmp1 = fields.get_tmp();
    auto tmp2 = fields.get_tmp();

    int kk = 0;
    TF* f1  = &(tmp1->fld.data()[kk*agd.ijcells]); kk+=1;
    TF* f2  = &(tmp1->fld.data()[kk*agd.ijcells]); kk+=1;
    TF* f2b = &(tmp1->fld.data()[kk*agd.ijcells]); kk+=1;
    TF* f3  = &(tmp1->fld.data()[kk*agd.ijcells]); kk+=1;

    TF* theta_mean_n = &(tmp1->fld.data()[kk*agd.ijcells]); kk+=1;

    // Get required thermo fields in 2D slices of tmp field.
    thermo.get_land_surface_fields(*tmp2);

    TF* T_bot = tmp2->fld_bot.data();
    TF* T_a = tmp2->fld_top.data();
    TF* vpd = tmp2->flux_bot.data();
    TF* qsat_bot = tmp2->flux_top.data();
    TF* dqsatdT_bot = tmp2->grad_bot.data();

    const std::vector<TF>& rhorefh = thermo.get_rhorefh_vector();

    // Get surface aerodynamic resistance (calculated in tmp1->flux_bot...)
    boundary.get_ra(*tmp1);
    TF* ra = tmp1->flux_bot.data();

    // Get surface precipitation
    microphys.get_surface_rain_rate(tmp1->fld_bot);
    TF* rain_rate = tmp1->fld_bot.data();

    // Calculate root fraction weighted mean soil water content
    soil::calc_root_weighted_mean_theta(
            theta_mean_n,
            fields.sps.at("theta")->fld.data(),
            soil_index.data(),
            root_fraction.data(),
            theta_wp.data(),
            theta_fc.data(),
            agd.istart, agd.iend,
            agd.jstart, agd.jend,
            sgd.kstart, sgd.kend,
            agd.icells, agd.ijcells);

    // Calculate vegetation/soil resistance functions `f`
    lsm::calc_resistance_functions(
            f1, f2, f2b, f3,
            sw_dn.data(),
            fields.sps.at("theta")->fld.data(),
            theta_mean_n,
            vpd,
            gD_coeff.data(),
            c_veg.data(),
            theta_wp.data(),
            theta_fc.data(),
            theta_res.data(),
            soil_index.data(),
            agd.istart, agd.iend,
            agd.jstart, agd.jend,
            sgd.kend,
            agd.icells, agd.ijcells);

    // Calculate canopy resistance per tile
    lsm::calc_canopy_resistance(
            tiles.at("veg").rs.data(),
            rs_veg_min.data(), lai.data(),
            f1, f2, f3,
            agd.istart, agd.iend,
            agd.jstart, agd.jend,
            agd.icells);

    lsm::calc_soil_resistance(
            tiles.at("soil").rs.data(),
            rs_soil_min.data(), f2b,
            agd.istart, agd.iend,
            agd.jstart, agd.jend,
            agd.icells);

    // Solve the surface energy balance
    for (auto& tile : tiles)
        lsm::calc_fluxes(
                tile.second.H.data(), tile.second.LE.data(),
                tile.second.G.data(), T_a,
                fields.sp.at("qt")->fld.data(),
                fields.sps.at("t")->fld.data(),
                T_bot, qsat_bot, dqsatdT_bot,
                ra, tile.second.rs.data(), lambda.data(),
                sw_dn.data(), sw_up.data(),
                lw_dn.data(), lw_up.data(),
                rhorefh.data(),
                agd.istart, agd.iend,
                agd.jstart, agd.jend,
                agd.kstart, sgd.kend,
                agd.icells, agd.ijcells);

    // Calculate dynamic tile fractions
    lsm::calc_tile_fractions(
            tiles.at("veg").fraction.data(),
            tiles.at("soil").fraction.data(),
            tiles.at("wet").fraction.data(),
            fields.ap2d.at("wl").data(),
            c_veg.data(),
            lai.data(),
            agd.istart, agd.iend,
            agd.jstart, agd.jend,
            agd.icells);

    // Calculate changes in the liquid water reservoir
    const double subdt = timeloop.get_sub_time_step();

    lsm::calc_liquid_water_reservoir(
            fields.at2d.at("wl").data(),
            interception.data(),
            throughfall.data(),
            fields.ap2d.at("wl").data(),
            tiles.at("veg").LE.data(),
            tiles.at("soil").LE.data(),
            tiles.at("wet").LE.data(),
            tiles.at("veg").fraction.data(),
            tiles.at("soil").fraction.data(),
            tiles.at("wet").fraction.data(),
            rain_rate, c_veg.data(), lai.data(), subdt,
            agd.istart, agd.iend,
            agd.jstart, agd.jend,
            agd.icells);

    // Solve bottom boundary condition back
    lsm::calc_bcs(
            fields.sp.at("thl")->fld_bot.data(),
            fields.sp.at("qt")->fld_bot.data(),
            fields.sp.at("thl")->fld.data(),
            fields.sp.at("qt")->fld.data(),
            tiles.at("veg").H.data(),
            tiles.at("soil").H.data(),
            tiles.at("wet").H.data(),
            tiles.at("veg").LE.data(),
            tiles.at("soil").LE.data(),
            tiles.at("wet").LE.data(),
            tiles.at("veg").fraction.data(),
            tiles.at("soil").fraction.data(),
            tiles.at("wet").fraction.data(),
            ra, rhorefh.data(),
            agd.istart, agd.iend,
            agd.jstart, agd.jend,
            agd.kstart,
            agd.icells, agd.ijcells);

    fields.release_tmp(tmp1);
    fields.release_tmp(tmp2);

}

template<typename TF>
void Land_surface<TF>::exec_stats(Stats<TF>& stats)
{
    if (!sw_land_surface)
        return;

    const TF offset = 0;
    auto& agd = grid.get_grid_data();
    auto tmp1 = fields.get_tmp();

    // Soil prognostic fields
    stats.calc_stats_soil("t",     fields.sps.at("t")->fld,     offset);
    stats.calc_stats_soil("theta", fields.sps.at("theta")->fld, offset);

    // Non-tiled variables
    stats.calc_stats_2d("wl", fields.ap2d.at("wl"), offset);

    get_tiled_mean(tmp1->fld_bot, "H");
    stats.calc_stats_2d("H", tmp1->fld_bot, offset);

    get_tiled_mean(tmp1->fld_bot, "LE");
    stats.calc_stats_2d("LE", tmp1->fld_bot, offset);

    get_tiled_mean(tmp1->fld_bot, "G");
    stats.calc_stats_2d("G", tmp1->fld_bot, offset);

    // Tiled variables
    std::string name;
    for (auto& tile : tiles)
    {
        stats.calc_stats_2d("c_" +tile.first, tile.second.fraction, offset);
        stats.calc_stats_2d("H_" +tile.first, tile.second.H, offset);
        stats.calc_stats_2d("LE_"+tile.first, tile.second.LE, offset);
        stats.calc_stats_2d("G_" +tile.first, tile.second.G, offset);
        stats.calc_stats_2d("rs_"+tile.first, tile.second.rs, offset);
    }

    fields.release_tmp(tmp1);
}

template<typename TF>
void Land_surface<TF>::exec_column(Column<TF>& column)
{
    if (!sw_land_surface)
        return;

    const TF offset = 0;
    auto& agd = grid.get_grid_data();
    auto tmp1 = fields.get_tmp();

    // Non-tiled variables
    column.calc_time_series("wl", fields.ap2d.at("wl").data(), offset);

    get_tiled_mean(tmp1->fld_bot, "H");
    column.calc_time_series("H", tmp1->fld_bot.data(), offset);

    get_tiled_mean(tmp1->fld_bot, "LE");
    column.calc_time_series("LE", tmp1->fld_bot.data(), offset);

    get_tiled_mean(tmp1->fld_bot, "G");
    column.calc_time_series("G", tmp1->fld_bot.data(), offset);

    fields.release_tmp(tmp1);
}

template<typename TF>
void Land_surface<TF>::exec_cross(Cross<TF>& cross, unsigned long iotime)
{
    if (!sw_land_surface)
        return;

    for (auto& it : crosslist)
    {
        if (it == "t_soil")
            cross.cross_soil(fields.sps.at("t")->fld.data(), it, iotime);
        else if (it == "theta_soil")
            cross.cross_soil(fields.sps.at("theta")->fld.data(), it, iotime);
    }
}

template<typename TF>
void Land_surface<TF>::get_tiled_mean(std::vector<TF>& mean, std::string name)
{
    auto& agd = grid.get_grid_data();

    if (name == "H")
        lsm::calc_tiled_mean(
                mean.data(),
                tiles.at("veg").H.data(),
                tiles.at("soil").H.data(),
                tiles.at("wet").H.data(),
                tiles.at("veg").fraction.data(),
                tiles.at("soil").fraction.data(),
                tiles.at("wet").fraction.data(),
                agd.istart, agd.iend,
                agd.jstart, agd.jend,
                agd.icells);

    else if (name == "LE")
        lsm::calc_tiled_mean(
                mean.data(),
                tiles.at("veg").LE.data(),
                tiles.at("soil").LE.data(),
                tiles.at("wet").LE.data(),
                tiles.at("veg").fraction.data(),
                tiles.at("soil").fraction.data(),
                tiles.at("wet").fraction.data(),
                agd.istart, agd.iend,
                agd.jstart, agd.jend,
                agd.icells);

    else if (name == "G")
        lsm::calc_tiled_mean(
                mean.data(),
                tiles.at("veg").G.data(),
                tiles.at("soil").G.data(),
                tiles.at("wet").G.data(),
                tiles.at("veg").fraction.data(),
                tiles.at("soil").fraction.data(),
                tiles.at("wet").fraction.data(),
                agd.istart, agd.iend,
                agd.jstart, agd.jend,
                agd.icells);

    else
    {
        std::string err = "Cannot get tiled mean of variable \"" + name + "\"";
        throw std::runtime_error(err);
    }
}

template<typename TF>
void Land_surface<TF>::save(const int itime)
{
    if (!sw_land_surface)
        return;

    auto field3d_io = Field3d_io<TF>(master, grid);
    auto& sgd = soil_grid.get_grid_data();

    const TF no_offset = 0.;
    int nerror = 0;

    auto tmp1 = fields.get_tmp();
    auto tmp2 = fields.get_tmp();

    // Save the 3D soil fields (temperature and moisture)
    auto save_3d_field = [&](TF* const restrict field, const std::string& name)
    {
        char filename[256];
        std::sprintf(filename, "%s.%07d", name.c_str(), itime);
        master.print_message("Saving \"%s\" ... ", filename);

        if (field3d_io.save_field3d(
                field, tmp1->fld.data(), tmp2->fld.data(),
                filename, no_offset,
                sgd.kstart, sgd.kend))
        {
            master.print_message("FAILED\n");
            nerror += 1;
        }
        else
            master.print_message("OK\n");
    };

    save_3d_field(fields.sps.at("t")->fld.data(), "t_soil");
    save_3d_field(fields.sps.at("theta")->fld.data(), "theta_soil");

    // Surface temperature, humidity and liquid water content.
    auto save_2d_field = [&](TF* const restrict field, const std::string& name)
    {
        char filename[256];
        std::sprintf(filename, "%s.%07d", name.c_str(), itime);
        master.print_message("Saving \"%s\" ... ", filename);

        const int kslice = 0;
        if (field3d_io.save_xy_slice(
                field, tmp1->fld.data(),
                filename, kslice))
        {
            master.print_message("FAILED\n");
            nerror += 1;
        }
        else
            master.print_message("OK\n");
    };

    save_2d_field(fields.ap2d.at("wl").data(), "wl_skin");
    save_2d_field(fields.sp.at("thl")->fld_bot.data(), "thl_bot");
    save_2d_field(fields.sp.at("qt")->fld_bot.data(), "qt_bot");

    fields.release_tmp(tmp1);
    fields.release_tmp(tmp2);

    master.sum(&nerror, 1);

    if (nerror)
        throw std::runtime_error("Error saving soil/surface fields");
}

template<typename TF>
void Land_surface<TF>::load(const int iotime)
{
    if (!sw_land_surface)
        return;

    auto field3d_io = Field3d_io<TF>(master, grid);
    auto& sgd = soil_grid.get_grid_data();

    const TF no_offset = 0.;
    int nerror = 0;

    auto tmp1 = fields.get_tmp();
    auto tmp2 = fields.get_tmp();
    auto tmp3 = fields.get_tmp();

    auto load_3d_field = [&](
            TF* const restrict field, const std::string& name, const int time)
    {
        char filename[256];
        std::sprintf(filename, "%s.%07d", name.c_str(), time);
        master.print_message("Loading \"%s\" ... ", filename);

        if (field3d_io.load_field3d(
                field,
                tmp1->fld.data(), tmp2->fld.data(),
                filename, no_offset,
                sgd.kstart, sgd.kend))
        {
            master.print_message("FAILED\n");
            nerror += 1;
        }
        else
            master.print_message("OK\n");
    };

    auto load_2d_field = [&](
            TF* const restrict field, const std::string& name, const int time)
    {
        char filename[256];
        std::sprintf(filename, "%s.%07d", name.c_str(), time);
        master.print_message("Loading \"%s\" ... ", filename);

        if (field3d_io.load_xy_slice(
                field, tmp1->fld.data(),
                filename))
        {
            master.print_message("FAILED\n");
            nerror += 1;
        }
        else
            master.print_message("OK\n");

        boundary_cyclic.exec_2d(field);
    };

    // Load the 3D soil temperature and moisture fields.
    load_3d_field(fields.sps.at("t")->fld.data(), "t_soil", iotime);
    load_3d_field(fields.sps.at("theta")->fld.data(), "theta_soil", iotime);

    // Load the surface temperature, humidity and liquid water content.
    load_2d_field(fields.ap2d.at("wl").data(), "wl_skin", iotime);
    load_2d_field(fields.sp.at("thl")->fld_bot.data(), "thl_bot", iotime);
    load_2d_field(fields.sp.at("qt")->fld_bot.data(), "qt_bot", iotime);

    // In case of heterogeneous land-surface, read spatial properties.
    if (!sw_homogeneous)
    {
        // 3D (soil) fields
        // BvS: yikes.. Read soil index as float, round/cast to int... TO-DO: fix this!
        load_3d_field(tmp3->fld.data(), "index_soil", 0);
        for (int i=0; i<sgd.ncells; ++i)
            soil_index[i] = std::round(tmp3->fld[i]);

        load_3d_field(root_fraction.data(), "root_frac", 0);

        // 2D (surface) fields
        load_2d_field(gD_coeff.data(), "gD", 0);
        load_2d_field(c_veg.data(), "c_veg", 0);
        load_2d_field(lai.data(), "lai", 0);
        load_2d_field(rs_veg_min.data(), "rs_veg_min", 0);
        load_2d_field(rs_soil_min.data(), "rs_soil_min", 0);
        load_2d_field(lambda.data(), "lambda_skin", 0);
    }

    fields.release_tmp(tmp1);
    fields.release_tmp(tmp2);
    fields.release_tmp(tmp3);

    master.sum(&nerror, 1);

    if (nerror)
        throw std::runtime_error("Error loading land surface fields");
}

template class Land_surface<double>;
template class Land_surface<float>;
