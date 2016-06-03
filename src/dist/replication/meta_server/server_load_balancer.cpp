/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Microsoft Corporation
 * 
 * -=- Robust Distributed System Nucleus (rDSN) -=- 
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * Description:
 *     base implementation of the server load balancer which defines the scheduling
 *     policy of how to place the partition replica to the nodes
 *
 * Revision history:
 *     2015-12-29, @imzhenyu (Zhenyu Guo), first draft
 *     xxxx-xx-xx, author, fix bug about xxx
 */

# include "server_load_balancer.h"

# ifdef __TITLE__
# undef __TITLE__
# endif
# define __TITLE__ "server.load.balancer"

namespace dsn
{
    namespace dist
    {

        bool server_load_balancer::s_lb_for_test = false;
        bool server_load_balancer::s_disable_lb = false;

        // meta server => partition server
        void server_load_balancer::send_proposal(::dsn::rpc_address node, const configuration_update_request& proposal)
        {
            ddebug("send proposal to %s: type = %s, node = %s, gpid = %u.%u, current ballot = %" PRId64,
                node.to_string(),
                enum_to_string(proposal.type),
                proposal.node.to_string(),
                proposal.config.pid.get_app_id(),
                proposal.config.pid.get_partition_index(),
                proposal.config.ballot
                );
            dassert(proposal.info.app_type != "", "make sure app-info is filled properly before sending the proposal");
            rpc::call_one_way_typed(node, RPC_CONFIG_PROPOSAL, proposal, gpid_to_hash(proposal.config.pid));
        }

        ::dsn::rpc_address server_load_balancer::find_minimal_load_machine(const partition_configuration& pc, bool primaryOnly)
        {
            std::vector<std::pair< ::dsn::rpc_address, int>> stats;

            for (auto it = _state->_nodes.begin(); it != _state->_nodes.end(); ++it)
            {
                if (it->second.is_alive &&
                    it->first != pc.primary &&
                    std::find(pc.secondaries.begin(), pc.secondaries.end(), it->first) == pc.secondaries.end())
                {
                    stats.push_back(std::make_pair(it->first, static_cast<int>(primaryOnly ? it->second.primaries.size()
                        : it->second.partitions.size())));
                }
            }

            if (stats.empty())
            {
                return ::dsn::rpc_address();
            }

            std::sort(stats.begin(), stats.end(), [](const std::pair< ::dsn::rpc_address, int>& l, const std::pair< ::dsn::rpc_address, int>& r)
            {
                return l.second < r.second || (l.second == r.second && l.first < r.first);
            });

            if (s_lb_for_test)
            {
                // alway use the first (minimal) one
                return stats[0].first;
            }

            int candidate_count = 1;
            int val = stats[0].second;

            for (size_t i = 1; i < stats.size(); i++)
            {
                if (stats[i].second > val)
                    break;
                candidate_count++;
            }

            return stats[dsn_random32(0, candidate_count - 1)].first;
        }

        void server_load_balancer::explictly_send_proposal(gpid gpid, rpc_address receiver, config_type::type type, rpc_address node)
        {
            if (gpid.get_app_id() <= 0 || gpid.get_partition_index() < 0 || type == config_type::CT_INVALID)
            {
                derror("invalid params");
                return;
            }

            configuration_update_request req;
            {
                zauto_read_lock l(_state->_lock);
                if (gpid.get_app_id() > _state->_apps.size())
                {
                    derror("invalid params");
                    return;
                }
                app_state& app = _state->_apps[gpid.get_app_id()-1];
                if (gpid.get_partition_index()>=app.info.partition_count)
                {
                    derror("invalid params");
                    return;
                }

                req.info = app.info;
                req.config = app.partitions[gpid.get_partition_index()];
            }

            req.type = type;
            req.node = node;
            send_proposal(receiver, req);
        }
    }
}
