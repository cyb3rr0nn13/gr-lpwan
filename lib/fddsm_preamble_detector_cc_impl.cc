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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gnuradio/io_signature.h>
#include "fddsm_preamble_detector_cc_impl.h"
#include <volk/volk.h>

namespace gr {
  namespace lpwan {

    filter_branch::filter_branch(uint32_t buf_len, std::vector<float> taps, uint32_t stepsize)
        : d_demod(fddsm_demodulator_kernel(2, false)),
          d_dotprod(sliding_dotprod_32f_x2_32f(taps)),
          d_buf(std::vector<float>(buf_len, -1)),
          d_preamble_correlation(0.0),
          d_stepsize(stepsize),
          d_threshold(1.0f),
          d_avg_corr(1.0f),
          d_var_corr(1.0f)
    {}

    void
    filter_branch::demodulate_soft(gr_complex *corr_in)
    {
      d_demod.demodulate_soft(&d_buf[0], corr_in, d_num_bits, d_stepsize);
    }

    void
    filter_branch::dotprod()
    {
      d_dotprod.dotprod(d_tmp, &d_buf[0], d_num_bits);
      d_preamble_correlation = d_tmp[1];
    }

    fddsm_preamble_detector_cc::sptr
    fddsm_preamble_detector_cc::make(std::vector<float> shr, unsigned int sps, unsigned int spreading_factor, unsigned int num_chips_gap, float alpha, float beta, std::vector<float> phase_increments)
    {
      return gnuradio::get_initial_sptr
        (new fddsm_preamble_detector_cc_impl(shr, sps, spreading_factor, num_chips_gap, alpha, beta, phase_increments));
    }

    /*
     * The private constructor
     */
    fddsm_preamble_detector_cc_impl::fddsm_preamble_detector_cc_impl(std::vector<float> shr, unsigned int sps, unsigned int spreading_factor, unsigned int num_chips_gap, float alpha, float beta, std::vector<float> phase_increments)
      : gr::sync_block("fddsm_preamble_detector_cc",
              gr::io_signature::makev(phase_increments.size()+1 ,phase_increments.size()+1, std::vector<int>(phase_increments.size()+1, sizeof(gr_complex))),
              gr::io_signature::make3(3, 3, sizeof(gr_complex), sizeof(float), sizeof(float))),
        d_shr(shr),
        d_sps(sps),
        d_spreading_factor(spreading_factor),
        d_num_chips_gap(num_chips_gap),
        d_alpha(alpha),
        d_beta(beta),
        d_frame_number(0),
        d_phase_increments(phase_increments),
        d_num_freq_hypotheses(phase_increments.size())
    {
      // num branches per frequency hypothesis == length of one space-time block in samples
      d_stepsize = d_sps * (d_spreading_factor + d_num_chips_gap);
      d_num_branches_per_hypothesis = 2 * d_stepsize;
      /*for(auto i = 0; i < d_num_freq_hypotheses; ++i)
      {
        d_avg_pp_power.push_back(std::vector<float>(d_stepsize, 1)); // initializing with zero may cause lots of false alarms at startup
        d_var_pp_power.push_back(std::vector<float>(d_stepsize, 1)); // do not initialize with 0 for same reason as above
      }*/

      // make sure that the SHR is NRZ with an amplitude equal to 1/sqrt(L * 2). This ensures equal input and output power.
      for(auto i=0; i < d_shr.size(); ++i)
      {
        d_shr[i] = d_shr[i] / std::abs(d_shr[i]) / std::sqrt((float) d_shr.size()) / std::sqrt(2.0f);
      }

      // initialize all the different branches with their intermediate buffers, FD-DSM decoders and SHR filters
      for(auto i = 0; i < d_num_freq_hypotheses; ++i)
      {
        std::vector<filter_branch> tmp;
        for(auto j = 0; j < d_num_branches_per_hypothesis; ++j)
        {
          tmp.push_back(filter_branch(2, d_shr, d_stepsize));
        }
        d_filter_branches.push_back(tmp);
      }

/*
      auto len_buf_per_filter_branch = 100;
      for(auto n = 0; n < d_num_freq_hypotheses; ++n) {
        d_freq_hypotheses.push_back(std::shared_ptr<)
        for (auto i = 0; i < d_num_branches_per_hypothesis; ++i) {
          d_buf.push_back(std::vector<float>(len_buf_per_filter_branch));
          d_demod_kernels.push_back(std::unique_ptr<fddsm_demodulator_kernel>(new fddsm_demodulator_kernel(2, false)));
          d_dotprod_kernels.push_back(
              std::unique_ptr<sliding_dotprod_32f_x2_32f>(new sliding_dotprod_32f_x2_32f(d_shr)));
        }
      }
*/

      // make sure that even the latest polyphase can access the following symbol for demodulation
      set_output_multiple(d_num_branches_per_hypothesis + d_stepsize);
    }

    /*
     * Our virtual destructor.
     */
    fddsm_preamble_detector_cc_impl::~fddsm_preamble_detector_cc_impl()
    {
    }

    int
    fddsm_preamble_detector_cc_impl::work(int noutput_items,
        gr_vector_const_void_star &input_items,
        gr_vector_void_star &output_items)
    {
      // FIXME: there has to be a better way that is shorter and preserves the const
      auto corr_in = new gr_complex*[d_num_freq_hypotheses];
      for(auto i = 0; i < d_num_freq_hypotheses; ++i)
      {
        corr_in[i] = (gr_complex*) input_items[i];
      }

      const auto *signal_in = (const gr_complex *) input_items[d_num_freq_hypotheses]; // the last input is the actual signal
      auto *signal_out = (gr_complex *) output_items[0];
      auto *corr_out = (float *) output_items[1];
      auto *threshold_out = (float *) output_items[2];

      // Copy signal from input to output
      auto nbits_to_process = 2; // be careful if this is to be changed (e.g., for performance reason). There are some implications that need to be dealt with, e.g. buffer sizes
      auto nitems_processed = nbits_to_process * d_stepsize;
      std::memcpy(signal_out, signal_in, sizeof(gr_complex) * nitems_processed);

      // Demodulate FD-DSM signal for the different frequency hypotheses, output soft bits to intermediate buffer and correlate with preamble.
      // Effectively, we create polyphase filterbanks by splitting/deinterleaving each input in 2 * d_stepsize polyphase
      // components, filtering each component and interleaving the filter/correlator output again.
      //auto nbits_to_process = std::min(static_cast<size_t>(noutput_items) / d_num_branches * 2, d_buf[0].size());

      // TODO: This part could be done in parallel by multiple threads as this will be the bottleneck
      // TODO: Assigning threads to frequency hypotheses also avoids possible race conditions.
      auto correlation_display_index = 0; // this is just an arbitrary choice for debug purposes
      for(auto i = 0; i < d_num_freq_hypotheses; ++i)
      {
        for(auto j = 0; j < d_num_branches_per_hypothesis; ++j)
        {
          // calculate d_preamble_correlation based on the soft bit output of the demodulator
          d_filter_branches[i][j].demodulate_soft(corr_in[i] + j);
          d_filter_branches[i][j].dotprod();

          // single-pole IIR for calculating the moments of the absolute value of the correlation for each polyphase component
          d_filter_branches[i][j].d_avg_corr = (1 - d_alpha) * d_filter_branches[i][j].d_avg_corr
                                               + std::abs(d_filter_branches[i][j].d_preamble_correlation) * d_alpha;
          d_filter_branches[i][j].d_var_corr = (1 - d_alpha) * d_filter_branches[i][j].d_var_corr
                                               + std::pow(std::abs(d_filter_branches[i][j].d_preamble_correlation - d_filter_branches[i][j].d_avg_corr), 2.0f) * d_alpha;

          // threshold is determined by considering the absolute value of the preamble correlation as a normally distributed random variable
          d_filter_branches[i][j].d_threshold = d_filter_branches[i][j].d_avg_corr + d_beta * std::sqrt(d_filter_branches[i][j].d_var_corr);

          if(i == correlation_display_index)
          {
            corr_out[j] = d_filter_branches[i][j].d_preamble_correlation;
            threshold_out[j] = d_filter_branches[i][j].d_threshold;
          }

          if(d_filter_branches[i][j].d_preamble_correlation > d_filter_branches[i][j].d_threshold)
          {
            // exclude the peak from the threshold calculation as it does not belong to the "average" behavior
            d_filter_branches[i][j].d_avg_corr -= std::abs(d_filter_branches[i][j].d_preamble_correlation) * d_alpha;
            d_filter_branches[i][j].d_var_corr -= std::pow(std::abs(d_filter_branches[i][j].d_preamble_correlation - d_filter_branches[i][j].d_avg_corr), 2.0f) * d_alpha;

            // a preamble was detected at offset j on branch i; attach a tag including the phase increment of the respective filter branch and the last symbols required for demodulation
            auto tag_dict = pmt::make_dict();
            auto value = pmt::make_tuple(
                pmt::from_complex(std::real(corr_in[i][j]), std::imag(corr_in[i][j])),
                pmt::from_complex(std::real(corr_in[i][j + d_stepsize]), std::imag(corr_in[i][j + d_stepsize])));
            tag_dict = pmt::dict_add(tag_dict, pmt::intern("init_symbols"), value);
            tag_dict = pmt::dict_add(tag_dict, pmt::intern("frame_number"), pmt::from_uint64(d_frame_number));
            tag_dict = pmt::dict_add(tag_dict, pmt::intern("delta_phi_index"), pmt::from_long(i));
            tag_dict = pmt::dict_add(tag_dict, pmt::intern("delta_phi"), pmt::from_float(d_phase_increments[i]));
            d_frame_number++;
            add_item_tag(0, nitems_written(0) + j, pmt::intern("sop"), tag_dict);
            add_item_tag(1, nitems_written(1) + j, pmt::intern("sop"), tag_dict);

            std::cout << "Preamble detected @" << nitems_read(0) + j << " on branch " << i << std::endl;
          }
        }
      }

      /*for(auto i = 0; i < d_num_branches_per_hypothesis; ++i)
      {
        d_demod_kernels[i]->demodulate_soft(&d_buf[i][0], corr_in + i, nbits_to_process, d_stepsize);
      }
      for(auto i = 0; i < nbits_to_process/2; ++i)
      {
        float tmp[2];
        for(auto j = 0; j < d_num_branches_per_hypothesis; ++j)
        {
          d_dotprod_kernels[j]->dotprod(tmp, &d_buf[j][0], 2);
          corr_out[i * d_num_branches_per_hypothesis + j] = tmp[1];
        }
      }*/


/*      // Find correlation peaks that exceed the threshold and attach tags at the respective positions.
      for(auto i = 0; i < nitems_processed; ++i)
      {
        // single-pole IIR for each polyphase's power
        d_avg_pp_power[i % d_stepsize] = (1 - d_alpha) * d_avg_pp_power[i % d_stepsize] + std::abs(corr_out[i]) * d_alpha;
        d_var_pp_power[i % d_stepsize] = (1 - d_alpha) * d_var_pp_power[i % d_stepsize] + std::pow(std::abs(corr_out[i] - d_avg_pp_power[i % d_stepsize]), 2) * d_alpha;
        // beta can be compared to a sigma-factor to reduce false alarm probability
        threshold_out[i] = d_avg_pp_power[i % d_stepsize] + d_beta * std::sqrt(d_var_pp_power[i % d_stepsize]);
        if(corr_out[i] > threshold_out[i])
        {
          // exclude the peak from the threshold calculation as it does not belong to the "average" behavior
          // note that the peak still influenced the threshold for the current decision
          d_avg_pp_power[i % d_stepsize] -= std::abs(corr_out[i]) * d_alpha;
          d_var_pp_power[i % d_stepsize] -= std::pow(std::abs(corr_out[i] - d_avg_pp_power[i % d_stepsize]), 2) * d_alpha;

          auto offset = i;
          auto tag_dict = pmt::make_dict();
          auto value = pmt::make_tuple(
              pmt::from_complex(std::real(corr_in[offset]), std::imag(corr_in[offset])),
              pmt::from_complex(std::real(corr_in[offset + d_stepsize]), std::imag(corr_in[offset + d_stepsize])));
          tag_dict = pmt::dict_add(tag_dict, pmt::intern("init_symbols"), value);
          tag_dict = pmt::dict_add(tag_dict, pmt::intern("frame_number"), pmt::from_uint64(d_frame_number));
          d_frame_number++;
          add_item_tag(0, nitems_written(0) + offset, pmt::intern("sop"), tag_dict);
          add_item_tag(1, nitems_written(1) + offset, pmt::intern("sop"), tag_dict);
        }

      }*/

      // Tell runtime system how many output items we produced.
      return nitems_processed;
    }

  } /* namespace lpwan */
} /* namespace gr */

