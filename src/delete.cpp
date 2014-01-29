#include "proxy.hpp"

namespace elliptics {
void proxy::req_delete::on_request(const ioremap::swarm::http_request &req, const boost::asio::const_buffer &buffer) {
	try {
		server()->logger().log(ioremap::swarm::SWARM_LOG_INFO, "Delete: handle request: %s", req.url().to_string().c_str());
		auto file_info = server()->get_file_info(req);

		if (file_info.second.name.empty()) {
			server()->logger().log(ioremap::swarm::SWARM_LOG_INFO, "Delete: cannot determine a namespace");
			send_reply(400);
			return;
		}

		if (!server()->check_basic_auth(file_info.second.name, file_info.second.auth_key, req.headers().get("Authorization"))) {
			ioremap::swarm::http_response reply;
			ioremap::swarm::http_headers headers;

			reply.set_code(401);
			headers.add("WWW-Authenticate", std::string("Basic realm=\"") + file_info.second.name + "\"");
			reply.set_headers(headers);
			send_reply(std::move(reply));
		}

		auto session = server()->get_session();

		std::string filename;
		{
			auto &group_key = file_info.first;
			auto pos = group_key.find('/');

			if (pos == std::string::npos) {
				server()->logger().log(ioremap::swarm::SWARM_LOG_INFO, "Delete: cannot determine a group from key");
				send_reply(400);
				return;
			}

			group_key.substr(pos + 1).swap(filename);

			{
				auto group = group_key.substr(0, pos);
				try {
					session.set_groups(server()->get_groups(boost::lexical_cast<int>(group), filename));
				} catch (...) {
					server()->logger().log(ioremap::swarm::SWARM_LOG_INFO, "Delete: cannot determine a group from key");
					send_reply(400);
					return;
				}
			}
		}
		auto key = ioremap::elliptics::key(file_info.second.name + '.' + filename);

		if (session.get_groups().empty()) {
			send_reply(404);
			return;
		}

		if (session.state_num() < server()->die_limit()) {
			throw std::runtime_error("Too low number of existing states");
		}

		session.set_filter(ioremap::elliptics::filters::all);

		server()->logger().log(ioremap::swarm::SWARM_LOG_DEBUG, "Delete: removing data");
		session.remove(key).connect(std::bind(&req_delete::on_finished, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
	} catch (const std::exception &ex) {
		server()->logger().log(ioremap::swarm::SWARM_LOG_ERROR, "Delete request error: %s", ex.what());
		send_reply(500);
	} catch (...) {
		server()->logger().log(ioremap::swarm::SWARM_LOG_ERROR, "Delete request error: unknown");
		send_reply(500);
	}
}

void proxy::req_delete::on_finished(const ioremap::elliptics::sync_remove_result &srr, const ioremap::elliptics::error_info &error) {
	(void)srr;
	if (error) {
		server()->logger().log(ioremap::swarm::SWARM_LOG_ERROR, "%s", error.message().c_str());
		send_reply(error.code() == -ENOENT ? 404 : 500);
		return;
	}
	server()->logger().log(ioremap::swarm::SWARM_LOG_DEBUG, "Delete: sending reply");
	send_reply(200);
}
} // namespace elliptics

