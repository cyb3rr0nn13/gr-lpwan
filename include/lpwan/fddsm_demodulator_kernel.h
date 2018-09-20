/* -*- c++ -*- */
/* 
 * Copyright 2018 <+YOU OR YOUR COMPANY+>.
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */


#ifndef INCLUDED_LPWAN_FDDSM_DEMODULATOR_KERNEL_H
#define INCLUDED_LPWAN_FDDSM_DEMODULATOR_KERNEL_H

#include <lpwan/api.h>
#include <cstring>

namespace gr {
  namespace lpwan {

    /*!
     * \brief Kernel for demodulating FD-DSM signals to soft bits (LLRs).
     * Every block of symbols is assumed to represent an entire frame after which the demodulator is reset.
     * The parameter bps refers to the bits per space-time symbol, i.e., log2(2*L).
     *
     */
    class LPWAN_API fddsm_demodulator_kernel
    {
    public:
      fddsm_demodulator_kernel(unsigned int bps, bool reset_after_each_call);
      ~fddsm_demodulator_kernel();

      void demodulate_soft(float* dst_softbits, const gr_complex* src_symbols, unsigned long num_bits, unsigned int stepsize);
      void reset() { d_yp[0] = gr_complex(1, 0); d_yp[1] = gr_complex(1, 0); }
      void set(const gr_complex* yp){ std::memcpy(d_yp, yp, 2 * sizeof(gr_complex)); }
      float get_phase(){ return d_phase; }

    private:
        unsigned int d_bps;
        bool d_reset_after_each_call;
        gr_complex d_yp[2];
        float d_phase;
    };

  } // namespace lpwan
} // namespace gr

#endif /* INCLUDED_LPWAN_FDDSM_DEMODULATOR_KERNEL_H */

