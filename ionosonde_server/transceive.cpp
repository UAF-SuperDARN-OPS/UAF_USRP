// tx_worker.cpp
// 
#include <uhd/utils/thread_priority.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/static.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/exception.hpp>
#include <boost/thread/thread.hpp>
#include <boost/program_options.hpp>
#include <boost/math/special_functions/round.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <iostream>
#include <string>
#include <fstream>
#include <complex>
#include <csignal>
#include <cmath>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fftw3.h>
#include <stdio.h>
#include <stdlib.h>

#include <thread>
#include "../include/global_variables.h"

typedef std::complex<int16_t>  sc16;
extern int verbose;

void tx_worker(
    unsigned int bufflen,
    uhd::tx_streamer::sptr tx_stream,
    uhd::time_spec_t start_time,
    std::complex<int16_t>* vec_ptr,
    int end
);

void rx_worker(
    uhd::rx_streamer::sptr rx_stream,
    unsigned int samps_per_pulse,
    std::vector<std::complex<int16_t>* >& recv_ptr
);

void transceive(
    uhd::usrp::multi_usrp::sptr usrp,
    uhd::tx_streamer::sptr tx_stream,
    uhd::rx_streamer::sptr rx_stream,
    unsigned int npulses,
    float pulse_time,
    //std::complex<int16_t>* txbuff,
    std::vector<std::complex<int16_t> >* txbuff0,
    std::vector<std::complex<int16_t> >* txbuff1,
    float tx_ontime,
    std::complex<int16_t>** outdata,
    size_t samps_per_pulse
){
    int debug = 0;
    int fverb = 1;

    if (debug){
        std::cout << "samps_per_pulse: " << samps_per_pulse << std::endl;
    }


    //create metadeta tags for transmit streams
    if (verbose) std::cout << "time spec debug woah doggy" << std::endl;
    //usrp->set_clock_source("internal");
    //usrp->set_time_now(uhd::time_spec_t(0.0));
    if (verbose) std::cout << "time spec debug woah doggy doggy" << std::endl;
    uhd::time_spec_t start_time = usrp->get_time_now() + 0.05;
    if (verbose) std::cout << "time spec debug woah" << std::endl;
    uhd::tx_metadata_t md;
    md.start_of_burst = true;
    md.end_of_burst = false;
    md.has_time_spec = true;
    md.time_spec = start_time;
    std::vector<std::complex<int16_t> *> vec_ptr;
    vec_ptr.resize(1);
    //vec_ptr[0] = &txbuff->front();
    
    usrp->set_gpio_attr("RXA","CTRL",0x0, TRTRIG_BIT); //GPIO mode
    usrp->set_gpio_attr("RXA","DDR",TRTRIG_BIT, TRTRIG_BIT); //Direction out
    
    //create metadata tags for receive stream
    uhd::rx_metadata_t rxmd;
    std::vector<std::complex<int16_t> > buff(samps_per_pulse,0);
    if (verbose) std::cout << "rx buff size: " << buff.size() << std::endl;
    if (verbose) std::cout << "tx buff size: " << txbuff0->size() << std::endl;
    uhd::stream_cmd_t stream_cmd = uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE;
    stream_cmd.num_samps = npulses*samps_per_pulse;
    stream_cmd.stream_now = false;
    stream_cmd.time_spec = start_time + 22 / usrp->get_rx_rate(); //Digital hardware delay is 22 samples long.  Found by experiment.
    
    //loop for every pulse in the sequence
    size_t spb;
    std::vector<std::complex<int16_t>* > rx_dptr;
    rx_dptr.resize(usrp->get_rx_num_channels());
    spb = tx_stream->get_max_num_samps();
    if (verbose) std::cout << "npulses: " << npulses << std::endl;
    if (fverb) std::cout << "pulse_time: " <<pulse_time<<std::endl;
    boost::thread_group rx_threads;
    boost::thread_group tx_threads;
    for (int ipulse=0; ipulse<npulses; ipulse++){
        if (debug) std::cout << "pulse number: " << ipulse << std::endl;
        for (size_t ichan=0; ichan<usrp->get_rx_num_channels(); ichan++){
         rx_dptr[ichan] = ipulse*samps_per_pulse + outdata[ichan];
        }
        
        float timeout = 4.1;
        
        usrp->set_command_time(start_time-50e-6,0);
        usrp->set_gpio_attr("RXA","OUT",T_BIT, 0x8100);
        
        if (ipulse==0){
            if (fverb) std::cout << "time spec: " << stream_cmd.time_spec.get_real_secs() << std::endl;
            if (fverb) std::cout << "Issuing stream command to start collecting samples\n";
            usrp->issue_stream_cmd(stream_cmd);
        }
        
        usrp->set_command_time(start_time+tx_ontime,0);
        usrp->set_gpio_attr("RXA","OUT",R_BIT, 0x8100);

// sig gen mimmic
// 2r=ct, r=100e3, c=3e8, --> t=667e-6
// off one default rect pulse later (34us)
  //      usrp->set_command_time(start_time+1667e-6,0);
  //      usrp->set_gpio_attr("RXA","OUT",TRIG_BIT,TRIG_BIT);
  //      usrp->set_command_time(start_time+1701e-6,0);
  //      usrp->set_gpio_attr("RXA","OUT",0x0,TRIG_BIT); 

/*Below is for testing with signal analyzer and varying by complentary pulse*/
/*

        uhd::time_spec_t temp_time;
        float temp_length;

        if (ipulse%2 == 0){
            temp_time = start_time-50e-6+4000e-6;
        	usrp->set_command_time(temp_time,0);
            if (fverb) std::cout << "even pulse on: " << temp_time.get_real_secs() << std::endl;
        	usrp->set_gpio_attr("RXA","OUT",TRIG_BIT,TRIG_BIT);
            temp_length = -temp_time.get_real_secs();
            temp_time = start_time-50e-6+4034e-6;
            temp_length += temp_time.get_real_secs();
        	usrp->set_command_time(temp_time,0);
            if (fverb) std::cout << "even pulse off: " << temp_time.get_real_secs() << std::endl;
        	usrp->set_gpio_attr("RXA","OUT",0x0,TRIG_BIT);
            if (fverb) std::cout << "even pulse length was: " << temp_length << std::endl; 
	}
        if (ipulse%2 == 1){
            temp_time = start_time-50e-6+4000e-6;
        	usrp->set_command_time(temp_time,0);
            if (fverb) std::cout << "odd pulse on: " << temp_time.get_real_secs() << std::endl;
        	usrp->set_gpio_attr("RXA","OUT",TRIG_BIT,TRIG_BIT);
            temp_length = -temp_time.get_real_secs();
            temp_time = start_time-50e-6+4034e-6;
            temp_length += temp_time.get_real_secs();
        	usrp->set_command_time(temp_time,0);
            if (fverb) std::cout << "odd pulse off: " << temp_time.get_real_secs() << std::endl;
        	usrp->set_gpio_attr("RXA","OUT",0x0,TRIG_BIT);
            if (fverb) std::cout << "odd pulse length was: " << temp_length << std::endl;
	}                  

*/
        size_t acc_samps=0;
        if (ipulse%2 == 0) {
            vec_ptr[0] = &txbuff0->front();
        }
        if (ipulse%2 == 1) {
            vec_ptr[0] = &txbuff1->front();
        }
        
        if (ipulse != npulses-1) {
             tx_threads.create_thread(boost::bind(tx_worker,
                 txbuff0->size(), tx_stream, start_time, vec_ptr[0], 0));
        }
        if (ipulse == npulses-1) {
             tx_threads.create_thread(boost::bind(tx_worker,
                 txbuff0->size(), tx_stream, start_time, vec_ptr[0], 1));
        }
        
        rx_threads.join_all();
        rx_threads.create_thread(boost::bind(rx_worker,
         rx_stream, samps_per_pulse, rx_dptr));
        
        for (int k=0; k<samps_per_pulse; k++){
         std::complex<int16_t>* temp_ptr;
         temp_ptr = outdata[0]+ipulse*samps_per_pulse+k;
         //if (fverb) outtr<<"samp "<<(k+1)<<": "<<*temp_ptr<<std::endl;
        }

        //for (int k=0; k<10; k++){
        // //std::cout << "raw data: " << outdata[0][i][k] << "\t" << outdata[1][i][k] << std::endl;
        // std::cout << "raw data: " << rx_dptr[0][k] << " " << rx_dptr[1][k] << std::endl;
        //}
        //for (int k=0; k<samps_per_pulse; k++)
        //    outdata[i][k] += buff[k];
        
        
        start_time += float(pulse_time);
    }
    tx_threads.join_all();
    rx_threads.join_all();
}

/***********************************************************************
 * tx_worker function
 * A function to be used in its own thread for transmitting.  Push all
 * tx values into the USRP buffer as USRP buffer space is available,
 * but allow other actions to occur concurrently.
 **********************************************************************/
void tx_worker(
    unsigned int bufflen,
    uhd::tx_streamer::sptr tx_stream,
    uhd::time_spec_t start_time,
    std::complex<int16_t>* vec_ptr,
    int end
){
    unsigned int acc_samps = 0;

    uhd::tx_metadata_t md;
    md.start_of_burst = true;
    md.has_time_spec = true;
    md.time_spec = start_time;

    size_t spb = tx_stream->get_max_num_samps();
    if (spb > bufflen) spb = bufflen;

	while(acc_samps < bufflen-spb){
            size_t nsamples = tx_stream->send(vec_ptr, spb, md);
            vec_ptr += spb;
            acc_samps += nsamples;
            //std::cout << acc_samps <<std::endl;
            md.start_of_burst = false;
            md.has_time_spec = false;
    }
    // Now on the last packet
    if (end) md.end_of_burst = true;
    spb = bufflen - acc_samps;
    size_t nsamples = tx_stream->send(vec_ptr, spb, md);
}

void rx_worker(
    uhd::rx_streamer::sptr rx_stream,
    unsigned int samps_per_pulse,
    std::vector<std::complex<int16_t>* >& recv_ptr
){
    uhd::rx_metadata_t rxmd;
    float timeout = 4.1;
    rxmd.error_code = uhd::rx_metadata_t::ERROR_CODE_NONE;
    size_t nrx_samples = rx_stream->recv(recv_ptr, samps_per_pulse, rxmd, timeout);
    if (rxmd.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE){
        std::cerr << "Error!\t";
        std::cerr << rxmd.error_code << std::endl;
    }
}
