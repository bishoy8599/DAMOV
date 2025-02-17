#ifndef __HBM_MEMORY_H
#define __HBM_MEMORY_H

#include "HBM.h"
#include "Memory.h"
#include <vector>
#include "Statistics.h"
#include <fstream>
#include <array>
#include <climits>
#include <bitset>
#include <iterator>
#include <string>
#include <map>
#include <tuple>
#include <unordered_set>

using namespace std;

namespace ramulator
{

template<>
class Memory<HBM, Controller> : public MemoryBase
{
protected:
  ScalarStat dram_capacity;
  ScalarStat num_dram_cycles;
  ScalarStat num_incoming_requests;
  VectorStat num_read_requests;
  VectorStat num_write_requests;
  ScalarStat ramulator_active_cycles;
  ScalarStat memory_footprint;
  VectorStat incoming_requests_per_channel;
  VectorStat incoming_read_reqs_per_channel;

  ScalarStat physical_page_replacement;
  ScalarStat maximum_bandwidth;
  ScalarStat read_bandwidth;
  ScalarStat write_bandwidth;
  // shared by all Controller objects
  ScalarStat read_transaction_bytes;
  ScalarStat write_transaction_bytes;
  ScalarStat row_hits;
  ScalarStat row_misses;
  ScalarStat row_conflicts;
  VectorStat read_row_hits;
  VectorStat read_row_misses;
  VectorStat read_row_conflicts;
  VectorStat write_row_hits;
  VectorStat write_row_misses;
  VectorStat write_row_conflicts;

  ScalarStat read_latency_avg;
  ScalarStat read_network_latency_avg;
  ScalarStat read_latency_ns_avg;
  ScalarStat read_latency_sum;
  ScalarStat read_network_latency_sum;
  ScalarStat read_queue_latency_sum;
  ScalarStat read_queue_latency_avg;
  ScalarStat queueing_latency_avg;
  ScalarStat queueing_latency_ns_avg;
  ScalarStat queueing_latency_sum;
  ScalarStat sub_count_1;
  ScalarStat sub_count_2;
  ScalarStat sub_count_3;
  ScalarStat sub_count_4;
 

  ScalarStat req_queue_length_avg;
  ScalarStat req_queue_length_sum;
  ScalarStat read_req_queue_length_avg;
  ScalarStat read_req_queue_length_sum;
  ScalarStat write_req_queue_length_avg;
  ScalarStat write_req_queue_length_sum;

  long max_address;
public:
    std::map<std::tuple<int, int, int>, std::vector<int>> addressAccCountTable;
    // vector<AddressAccCountEntry> addressAccCountTable;
    bool pim_mode_enabled = false;
    bool network_overhead = false;
    enum class Type {
        ChRaBaRoCo,
        RoBaRaCoCh,
        MAX,
    } type = Type::RoBaRaCoCh;

    enum class Translation {
      None,
      Random,
      MAX,
    } translation = Translation::None;

    std::map<string, Translation> name_to_translation = {
      {"None", Translation::None},
      {"Random", Translation::Random},
    };

    vector<int> free_physical_pages;
    long free_physical_pages_remaining;
    map<pair<int, long>, long> page_translation;

    vector<Controller<HBM>*> ctrls;
    HBM * spec;
    vector<int> addr_bits;

    int tx_bits;
    int cacheline_size;

    Memory(const Config& configs, vector<Controller<HBM>*> ctrls)
        : ctrls(ctrls),
          spec(ctrls[0]->channel->spec),
          addr_bits(int(HBM::Level::MAX))
    {

        // make sure 2^N channels/ranks
        // TODO support channel number that is not powers of 2
        int *sz = spec->org_entry.count;
        assert((sz[0] & (sz[0] - 1)) == 0);
        assert((sz[1] & (sz[1] - 1)) == 0);
        // validate size of one transaction
        int tx = (spec->prefetch_size * spec->channel_width / 8);
        tx_bits = calc_log2(tx);
        assert((1<<tx_bits) == tx);

        pim_mode_enabled = configs.pim_mode_enabled();
        network_overhead = configs.network_overhead_enabled();

        // If hi address bits will not be assigned to Rows
        // then the chips must not be LPDDRx 6Gb, 12Gb etc.
        if (type != Type::RoBaRaCoCh && spec->standard_name.substr(0, 5) == "LPDDR")
            assert((sz[int(HBM::Level::Row)] & (sz[int(HBM::Level::Row)] - 1)) == 0);

        max_address = spec->channel_width / 8;

        for (unsigned int lev = 0; lev < addr_bits.size(); lev++) {
          addr_bits[lev] = calc_log2(sz[lev]);
            max_address *= sz[lev];
        }

        addr_bits[int(HBM::Level::MAX) - 1] -= calc_log2(spec->prefetch_size);

        // Initiating translation
        if (configs.contains("translation")) {
          translation = name_to_translation[configs["translation"]];
        }
        if (translation != Translation::None) {
          // construct a list of available pages
          // TODO: this should not assume a 4KB page!
          free_physical_pages_remaining = max_address >> 12;

          free_physical_pages.resize(free_physical_pages_remaining, -1);
        }

        cacheline_size = configs.get_cacheline_size();

        // regStats
        dram_capacity
            .name("dram_capacity")
            .desc("Number of bytes in simulated DRAM")
            .precision(0)
            ;
        dram_capacity = max_address;

        num_dram_cycles
            .name("dram_cycles")
            .desc("Number of DRAM cycles simulated")
            .precision(0)
            ;
        num_incoming_requests
            .name("incoming_requests")
            .desc("Number of incoming requests to DRAM")
            .precision(0)
            ;
        num_read_requests
            .init(configs.get_core_num())
            .name("read_requests")
            .desc("Number of incoming read requests to DRAM per core")
            .precision(0)
            ;
        num_write_requests
            .init(configs.get_core_num())
            .name("write_requests")
            .desc("Number of incoming write requests to DRAM per core")
            .precision(0)
            ;
        incoming_requests_per_channel
            .init(sz[int(HBM::Level::Channel)])
            .name("incoming_requests_per_channel")
            .desc("Number of incoming requests to each DRAM channel")
            ;
        incoming_read_reqs_per_channel
            .init(sz[int(HBM::Level::Channel)])
            .name("incoming_read_reqs_per_channel")
            .desc("Number of incoming read requests to each DRAM channel")
            ;

        ramulator_active_cycles
            .name("ramulator_active_cycles")
            .desc("The total number of cycles that the DRAM part is active (serving R/W)")
            .precision(0)
            ;
        memory_footprint
            .name("memory_footprint")
            .desc("memory footprint in byte")
            .precision(0)
            ;
        physical_page_replacement
            .name("physical_page_replacement")
            .desc("The number of times that physical page replacement happens.")
            .precision(0)
            ;

        maximum_bandwidth
            .name("maximum_bandwidth")
            .desc("The theoretical maximum bandwidth (Bps)")
            .precision(0)
            ;
        read_bandwidth
            .name("read_bandwidth")
            .desc("Real read bandwidth(Bps)")
            .precision(0)
            ;
        write_bandwidth
            .name("write_bandwidth")
            .desc("Real write bandwidth(Bps)")
            .precision(0)
            ;

        // shared by all Controller objects

        read_transaction_bytes
            .name("read_transaction_bytes")
            .desc("The total byte of read transaction")
            .precision(0)
            ;
        write_transaction_bytes
            .name("write_transaction_bytes")
            .desc("The total byte of write transaction")
            .precision(0)
            ;

        row_hits
            .name("row_hits")
            .desc("Number of row hits")
            .precision(0)
            ;
        row_misses
            .name("row_misses")
            .desc("Number of row misses")
            .precision(0)
            ;
        row_conflicts
            .name("row_conflicts")
            .desc("Number of row conflicts")
            .precision(0)
            ;

        read_row_hits
            .init(configs.get_core_num())
            .name("read_row_hits")
            .desc("Number of row hits for read requests")
            .precision(0)
            ;
        read_row_misses
            .init(configs.get_core_num())
            .name("read_row_misses")
            .desc("Number of row misses for read requests")
            .precision(0)
            ;
        read_row_conflicts
            .init(configs.get_core_num())
            .name("read_row_conflicts")
            .desc("Number of row conflicts for read requests")
            .precision(0)
            ;

        write_row_hits
            .init(configs.get_core_num())
            .name("write_row_hits")
            .desc("Number of row hits for write requests")
            .precision(0)
            ;
        write_row_misses
            .init(configs.get_core_num())
            .name("write_row_misses")
            .desc("Number of row misses for write requests")
            .precision(0)
            ;
        write_row_conflicts
            .init(configs.get_core_num())
            .name("write_row_conflicts")
            .desc("Number of row conflicts for write requests")
            .precision(0)
            ;

        read_latency_sum
            .name("read_latency_sum")
            .desc("The memory latency cycles (in memory time domain) sum for all read requests in this channel")
            .precision(0)
            ;
        read_network_latency_sum
            .name("read_network_latency_sum")
            .desc("The read memory network latency cycles (in memory time domain) sum for all read requests in this channel")
            .precision(0)
            ;
        read_queue_latency_sum
            .name("read_queue_latency_sum")
            .desc("The read memory queue latency cycles (in memory time domain) sum for all read requests in this channel")
            .precision(0)
            ;
        read_queue_latency_avg
            .name("read_queue_latency_avg")
            .desc("The read memory queue latency cycles (in memory time domain) sum for all read requests in this channel")
            .precision(6)
            ;
        read_latency_avg
            .name("read_latency_avg")
            .desc("The average memory latency cycles (in memory time domain) per request for all read requests in this channel")
            .precision(6)
            ;
        read_network_latency_avg
            .name("read_network_latency_avg")
            .desc("The average memory network latency cycles (in memory time domain) per request for all read requests in this channel")
            .precision(6)
            ;
        sub_count_1
            .name("sub_count_1")
            .desc("count of movement with one vault once")
            .precision(3)
            ;
        sub_count_2
            .name("sub_count_2")
            .desc("count of movement with one vault more than one time")
            .precision(3)
            ;
        sub_count_3
            .name("sub_count_3")
            .desc("count of movement with N vaults once")
            .precision(3)
            ;
        sub_count_4
            .name("sub_count_4")
            .desc("count of movement with N vaults more than one time")
            .precision(3)
            ;
        queueing_latency_sum
            .name("queueing_latency_sum")
            .desc("The sum of cycles waiting in queue before first command issued")
            .precision(0)
            ;
        queueing_latency_avg
            .name("queueing_latency_avg")
            .desc("The average of cycles waiting in queue before first command issued")
            .precision(6)
            ;
        read_latency_ns_avg
            .name("read_latency_ns_avg")
            .desc("The average memory latency (ns) per request for all read requests in this channel")
            .precision(6)
            ;
        queueing_latency_ns_avg
            .name("queueing_latency_ns_avg")
            .desc("The average of time (ns) waiting in queue before first command issued")
            .precision(6)
            ;

        req_queue_length_sum
            .name("req_queue_length_sum")
            .desc("Sum of read and write queue length per memory cycle.")
            .precision(0)
            ;
        req_queue_length_avg
            .name("req_queue_length_avg")
            .desc("Average of read and write queue length per memory cycle.")
            .precision(6)
            ;

        read_req_queue_length_sum
            .name("read_req_queue_length_sum")
            .desc("Read queue length sum per memory cycle.")
            .precision(0)
            ;
        read_req_queue_length_avg
            .name("read_req_queue_length_avg")
            .desc("Read queue length average per memory cycle.")
            .precision(6)
            ;

        write_req_queue_length_sum
            .name("write_req_queue_length_sum")
            .desc("Write queue length sum per memory cycle.")
            .precision(0)
            ;
        write_req_queue_length_avg
            .name("write_req_queue_length_avg")
            .desc("Write queue length average per memory cycle.")
            .precision(6)
            ;

        for (auto ctrl : ctrls) {
          ctrl->read_transaction_bytes = &read_transaction_bytes;
          ctrl->write_transaction_bytes = &write_transaction_bytes;

          ctrl->row_hits = &row_hits;
          ctrl->row_misses = &row_misses;
          ctrl->row_conflicts = &row_conflicts;
          ctrl->read_row_hits = &read_row_hits;
          ctrl->read_row_misses = &read_row_misses;
          ctrl->read_row_conflicts = &read_row_conflicts;
          ctrl->write_row_hits = &write_row_hits;
          ctrl->write_row_misses = &write_row_misses;
          ctrl->write_row_conflicts = &write_row_conflicts;

          ctrl->read_latency_sum = &read_latency_sum;
          ctrl->read_queue_latency_sum = &read_queue_latency_sum;
          ctrl->queueing_latency_sum = &queueing_latency_sum;

          ctrl->req_queue_length_sum = &req_queue_length_sum;
          ctrl->read_req_queue_length_sum = &read_req_queue_length_sum;
          ctrl->write_req_queue_length_sum = &write_req_queue_length_sum;
        }
    }

    ~Memory()
    {
        for (auto ctrl: ctrls)
            delete ctrl;
        delete spec;
    }

    double clk_ns()
    {
        return spec->speed_entry.tCK;
    }

    void record_core(int coreid) {
      for (auto ctrl : ctrls) {
        ctrl->record_core(coreid);
      }
    }

    void tick()
    {
        ++num_dram_cycles;

        bool is_active = false;
        for (auto ctrl : ctrls) {
          is_active = is_active || ctrl->is_active();
          ctrl->tick();
        }
        if (is_active) {
          ramulator_active_cycles++;
        }
        // if (int(num_dram_cycles.value()) % 10000 == 0 && num_dram_cycles.value() !=0){
        //     std::ofstream output_file("sub_count.txt", std::ios_base::app);
        //     for (AddressAccCountEntry &e : addressAccCountTable) output_file << e.ctrl << ", " << e.bank << "," << e.column << ", " << e.row << "\n";
        //     addressAccCountTable.clear();
        //     // std::ostream_iterator<std::string> output_iterator(output_file, "\n");
        //     // std::copy(addressAccCountTable.begin(), addressAccCountTable.end(), output_iterator);
        //     // for (AddressAccCountEntry i = addressAccCountTable.begin(); i != addressAccCountTable.end(); ++i){
        //         // output_file << i->addr << "," << int(i->ctrl) << "\n";
        //     // }
        //     output_file.close();
               
        // }

    }

    void set_address_recorder () {}
    void set_application_name(string _app) {}

    bool send(Request req)
    {
        req.addr_vec.resize(addr_bits.size());
        req.burst_count = cacheline_size / (1 << tx_bits);
        long addr = req.addr;
        int coreid = req.coreid;

        // Each transaction size is 2^tx_bits, so first clear the lowest tx_bits bits
        clear_lower_bits(addr, tx_bits);

        switch(int(type)){
            case int(Type::ChRaBaRoCo):
                for (int i = addr_bits.size() - 1; i >= 0; i--)
                    req.addr_vec[i] = slice_lower_bits(addr, addr_bits[i]);
                break;
            case int(Type::RoBaRaCoCh):
                req.addr_vec[0] = slice_lower_bits(addr, addr_bits[0]);
                req.addr_vec[addr_bits.size() - 1] = slice_lower_bits(addr, addr_bits[addr_bits.size() - 1]);
                for (int i = 1; i <= int(HBM::Level::Row); i++)
                    req.addr_vec[i] = slice_lower_bits(addr, addr_bits[i]);
                break;
            default:
                assert(false);
        }


        if(pim_mode_enabled )
        {          
            req.hops = calculate_extra_movement_latency(req.coreid, req.childid, req.addr_vec[int(HBM::Level::Channel)], req.addr_vec[int(HBM::Level::BankGroup)], req.type == Request::Type::READ);
            if(req.type == Request::Type::READ)
            {
                read_network_latency_sum += req.hops;
            }
            addressAccCountTable_insert(req);
        }


	// if(ctrls[req.addr_vec[0]]->update_serving_requests(req)) {
        if(ctrls[req.addr_vec[0]]->enqueue(req)) {
            // tally stats here to avoid double counting for requests that aren't enqueued
            ++num_incoming_requests;

            if (req.type == Request::Type::READ) {
              ++num_read_requests[coreid];

	      ++incoming_read_reqs_per_channel[req.addr_vec[int(HBM::Level::Channel)]];
            }
            if (req.type == Request::Type::WRITE) {
              ++num_write_requests[coreid];
           }
            ++incoming_requests_per_channel[req.addr_vec[int(HBM::Level::Channel)]];
          return true;
        }
        return false;
    }

    void addressAccCountTable_insert(Request req){
        addressAccCountTable[std::make_tuple(req.addr_vec[int(HBM::Level::Bank)]*req.addr_vec[int(HBM::Level::BankGroup)], req.addr_vec[int(HBM::Level::Column)], req.addr_vec[int(HBM::Level::Row)])].push_back(req.addr_vec[int(HBM::Level::Channel)]);
        // addressAccCountTable.push_back(AddressAccCountEntry(, );
    }

    int calculate_extra_movement_latency(int source_p, int source_c, int destination_p, int destination_c, bool read){
        // source_p = source_p % int(HBM::Level::Channel);
        // destination_p = destination_p % int(HBM::Level::Channel);
        int channel_change_latency = 5;
        channel_change_latency = read? channel_change_latency + 1 : channel_change_latency;
        int bankgroup_change_latency = 0;
        return source_p == destination_p ? abs(source_c - destination_c) * bankgroup_change_latency : channel_change_latency; // * abs(source_p - destination_p)  ;
    }

    int pending_requests()
    {
        int reqs = 0;
        for (auto ctrl: ctrls)
            reqs += ctrl->readq.size() + ctrl->writeq.size() + ctrl->otherq.size() + ctrl->pending.size();
        return reqs;
    }

    void finish() {
        dram_capacity = max_address;
        int *sz = spec->org_entry.count;
        maximum_bandwidth = spec->speed_entry.rate * 1e6 * spec->channel_width * sz[int(HBM::Level::Channel)] / 8;

        long dram_cycles = num_dram_cycles.value();
        long total_read_req = num_read_requests.total();
        for (auto ctrl : ctrls) {
            ctrl->finish(dram_cycles);
        }
        read_bandwidth = read_transaction_bytes.value() * 1e9 / (dram_cycles * clk_ns());
        write_bandwidth = write_transaction_bytes.value() * 1e9 / (dram_cycles * clk_ns());
        read_latency_avg = read_latency_sum.value() / total_read_req;
        read_network_latency_avg = read_network_latency_sum.value() / total_read_req;
        read_queue_latency_avg = read_queue_latency_sum.value() / total_read_req;
        queueing_latency_avg = queueing_latency_sum.value() / total_read_req;
        read_latency_ns_avg = read_latency_avg.value() * clk_ns();
        queueing_latency_ns_avg = queueing_latency_avg.value() * clk_ns();
        req_queue_length_avg = req_queue_length_sum.value() / dram_cycles;
        read_req_queue_length_avg = read_req_queue_length_sum.value() / dram_cycles;
        write_req_queue_length_avg = write_req_queue_length_sum.value() / dram_cycles;

        int countKeysWithOneElement = 0;
        int countKeysWithSameElement = 0;
        int countKeysWithDifferentElementsNoDuplicates = 0;
        int countKeysWithDifferentElementsDuplicates = 0;

        for (const auto& entry : addressAccCountTable) {
            const std::vector<int>& values = entry.second;
            int size = values.size();

            // Task 1: Number of keys that contain 1 element in their list
            if (size == 1) {
                countKeysWithOneElement++;
            }

            // Task 2: Number of keys that contain the same element in their list
            else if (size >= 2 && std::all_of(values.begin(), values.end(), [&values](int val) { return val == values[0]; })) {
                countKeysWithSameElement++;
            }

            // Task 3: Number of keys that contain different elements with no duplicate values
            else if (size >= 2) {
                std::unordered_set<int> uniqueValues(values.begin(), values.end());
                if (uniqueValues.size() == size) {
                    countKeysWithDifferentElementsNoDuplicates++;
                }

                // Task 4: Number of keys that contain different elements with duplicate values
                else {
                    countKeysWithDifferentElementsDuplicates++;
                }
            }
        }
        sub_count_1 = (countKeysWithOneElement *100/ addressAccCountTable.size());
        sub_count_2 = (countKeysWithSameElement*100 / addressAccCountTable.size());
        sub_count_3 = (countKeysWithDifferentElementsNoDuplicates*100 / addressAccCountTable.size()) ;
        sub_count_4 = (countKeysWithDifferentElementsDuplicates*100 / addressAccCountTable.size());


    }

    long page_allocator(long addr, int coreid) {
        long virtual_page_number = addr >> 12;

        switch(int(translation)) {
            case int(Translation::None): {
              auto target = make_pair(coreid, virtual_page_number);
              if(page_translation.find(target) == page_translation.end()) {
                memory_footprint += 1<<12;
                page_translation[target] = virtual_page_number;
              }
              return addr;
            }
            case int(Translation::Random): {
                auto target = make_pair(coreid, virtual_page_number);
                if(page_translation.find(target) == page_translation.end()) {
                    // page doesn't exist, so assign a new page
                    // make sure there are physical pages left to be assigned

                    // if physical page doesn't remain, replace a previous assigned
                    // physical page.
                    memory_footprint += 1<<12;
                    if (!free_physical_pages_remaining) {
                      physical_page_replacement++;
                      long phys_page_to_read = lrand() % free_physical_pages.size();
                      assert(free_physical_pages[phys_page_to_read] != -1);
                      page_translation[target] = phys_page_to_read;
                    } else {
                        // assign a new page
                        long phys_page_to_read = lrand() % free_physical_pages.size();
                        // if the randomly-selected page was already assigned
                        if(free_physical_pages[phys_page_to_read] != -1) {
                            long starting_page_of_search = phys_page_to_read;

                            do {
                                // iterate through the list until we find a free page
                                // TODO: does this introduce serious non-randomness?
                                ++phys_page_to_read;
                                phys_page_to_read %= free_physical_pages.size();
                            }
                            while((phys_page_to_read != starting_page_of_search) && free_physical_pages[phys_page_to_read] != -1);
                        }

                        assert(free_physical_pages[phys_page_to_read] == -1);

                        page_translation[target] = phys_page_to_read;
                        free_physical_pages[phys_page_to_read] = coreid;
                        --free_physical_pages_remaining;
                    }
                }

                // SAUGATA TODO: page size should not always be fixed to 4KB
                return (page_translation[target] << 12) | (addr & ((1 << 12) - 1));
            }
            default:
                assert(false);
        }

    }

private:

    int calc_log2(int val){
        int n = 0;
        while ((val >>= 1))
            n ++;
        return n;
    }
    int slice_lower_bits(long& addr, int bits)
    {
        int lbits = addr & ((1<<bits) - 1);
        addr >>= bits;
        return lbits;
    }
    void clear_lower_bits(long& addr, int bits)
    {
        addr >>= bits;
    }
    long lrand(void) {
        if(sizeof(int) < sizeof(long)) {
            return static_cast<long>(rand()) << (sizeof(int) * 8) | rand();
        }

        return rand();
    }
};

} /*namespace ramulator*/

#endif /*__HBM_MEMORY_H*/
