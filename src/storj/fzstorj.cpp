#include <stdio.h>

#include "events.hpp"

#include <libfilezilla/format.hpp>
#include <libfilezilla/mutex.hpp>

#include "storj.h"

#include <map>

fz::mutex output_mutex;

void fzprintf(storjEvent event)
{
	fz::scoped_lock l(output_mutex);

	fputc('0' + static_cast<int>(event), stdout);

	fflush(stdout);
}

template<typename ...Args>
void fzprintf(storjEvent event, Args &&... args)
{
	fz::scoped_lock l(output_mutex);

	fputc('0' + static_cast<int>(event), stdout);

	std::string s = fz::sprintf(std::forward<Args>(args)...);
	fwrite(s.c_str(), s.size(), 1, stdout);

	fputc('\n', stdout);
	fflush(stdout);
}

bool getLine(std::string & line)
{
	line.clear();
	while (true) {
		int c = fgetc(stdin);
		if (c == -1) {
			return false;
		}
		else if (!c) {
			return line.empty();
		}
		else if (c == '\n') {
			return !line.empty();
		}
		else if (c == '\r') {
			continue;
		}
		else {
			line += static_cast<unsigned char>(c);
		}
	}
}

std::string next_argument(std::string & line)
{
	std::string ret;

	fz::trim(line);

	if (line[0] == '"') {
		size_t pos = 1;
		size_t pos2;
		while ((pos2 = line.find('"', pos)) != std::string::npos && line[pos2 + 1] == '"') {
			ret += line.substr(pos, pos2 - pos + 1);
			pos = pos2 + 2;
		}
		if (pos2 == std::string::npos || (line[pos2 + 1] != ' ' && line[pos2 + 1] != '\0')) {
			line.clear();
			ret.clear();
		}
		ret += line.substr(pos, pos2 - pos);
		line = line.substr(pos2 + 1);
	}
	else {
		size_t pos = line.find(' ');
		if (pos == std::string::npos) {
			ret = line;
			line.clear();
		}
		else {
			ret = line.substr(0, pos);
			line = line.substr(pos + 1);
		}
	}

	fz::trim(line);

	return ret;
}

namespace {
extern "C" void get_buckets_callback(uv_work_t *work_req, int status)
{
	if (status != 0) {
		fzprintf(storjEvent::Error, "Request failed with outer status code %d", status);
	}
	else {
		get_buckets_request_t *req = static_cast<get_buckets_request_t *>(work_req->data);

		if (req->status_code != 200) {
			fzprintf(storjEvent::Error, "Request failed with status code %d", req->status_code);
		}
		else {
			bool encrypted = false;

			for (unsigned int i = 0; i < req->total_buckets; ++i) {
				storj_bucket_meta_t &bucket = req->buckets[i];


				if (!bucket.decrypted) {
					encrypted = true;
				}

				std::string id = bucket.id;
				std::string name = bucket.name;
				std::string created;
				if (bucket.created) {
					created = bucket.created;
				}
				fz::replace_substrings(name, "\r", "");
				fz::replace_substrings(id, "\r", "");
				fz::replace_substrings(created, "\r", "");
				fz::replace_substrings(name, "\n", "");
				fz::replace_substrings(id, "\n", "");
				fz::replace_substrings(created, "\n", "");

				auto perms = "id:" + id;
				if (encrypted) {
					fz::replace_substrings(name, "/", "_");
					perms += " nokey";
				}

				fzprintf(storjEvent::Listentry, "%s\n-1\n%s\n%s", name, perms, created);
			}

			if (encrypted) {
				fzprintf(storjEvent::Status, "Warning: Wrong encryption key for at least one bucket");
			}
			fzprintf(storjEvent::Done);
		}

		json_object_put(req->response);
		free(req);
	}

	free(work_req);
}

extern "C" void list_files_callback(uv_work_t *work_req, int status)
{
	if (status != 0) {
		fzprintf(storjEvent::Error, "Request failed with outer status code %d", status);
	}
	else {
		list_files_request_t *req = static_cast<list_files_request_t *>(work_req->data);

		std::string const& prefix = *static_cast<std::string const*>(req->handle);

		if (req->status_code != 200) {
			fzprintf(storjEvent::Error, "Request failed with status code %d", req->status_code);
		}
		else {
			std::map<std::string, std::pair<std::string, std::string>> dirs;

			bool prefixIsValid = prefix.empty();
			bool encrypted = false;
			for (unsigned int i = 0; i < req->total_files; ++i) {
				storj_file_meta_t &file = req->files[i];

				if (!file.decrypted) {
					encrypted = true;
					break;
				}

				std::string name = file.filename;
				std::string id = file.id;
				uint64_t size = file.size;
				std::string created;
				if (file.created) {
					created = file.created;
				}
				fz::replace_substrings(name, "\r", "");
				fz::replace_substrings(id, "\r", "");
				fz::replace_substrings(created, "\r", "");
				fz::replace_substrings(name, "\n", "");
				fz::replace_substrings(id, "\n", "");
				fz::replace_substrings(created, "\n", "");

				if (name.empty() || name == "." || name == "..") {
					continue;
				}

				if (!prefix.empty()) {
					if (name.substr(0, prefix.size()) != prefix) {
						continue;
					}
					name = name.substr(prefix.size());
					if (name.empty()) {
						prefixIsValid = true;
						fzprintf(storjEvent::Listentry, ".\n-1\n%s\n", id);
						continue;
					}
				}

				auto perms = "id:" + id;

				auto pos = name.find('/');
				if (pos != std::string::npos) {
					bool actualDirEntry = pos + 1 == name.size();
					name = name.substr(0, pos);
					if (name.empty()) {
						continue;
					}

					if (actualDirEntry) {
						dirs[name] = std::make_pair(perms, created);
					}
					else {
						dirs.insert(std::make_pair(name, std::make_pair(std::string(), std::string())));
					}
					continue;
				}

				prefixIsValid = true;
				fzprintf(storjEvent::Listentry, "%s\n%d\n%s\n%s", name, size, perms, created);
			}

			if (encrypted) {
				fzprintf(storjEvent::Error, "Wrong encryption key for this bucket");
			}
			else {
				if (!dirs.empty()) {
					prefixIsValid = true;
				}

				if (!prefixIsValid) {
					fzprintf(storjEvent::Error, "Directory does not exist");
				}
				else {
					for (auto const& dir : dirs) {
						fzprintf(storjEvent::Listentry, "%s/\n-1\n%s\n%s", dir.first, dir.second.first, dir.second.second);
					}
					fzprintf(storjEvent::Done);
				}
			}
		}

		json_object_put(req->response);
		free(req->path);
		free(req);
	}
	free(work_req);
}


extern "C" void download_file_progress(double progress,
								   uint64_t downloaded_bytes,
								   uint64_t total_bytes,
								   void *handle)
{
	uint64_t & lastProgress = *static_cast<uint64_t*>(handle);
	if (downloaded_bytes > lastProgress) {
		fzprintf(storjEvent::Transfer, "%u", downloaded_bytes - lastProgress);
		lastProgress = downloaded_bytes;
	}
}

extern "C" void download_file_complete(int status, FILE *fd, void *)
{
	if (status) {
		fzprintf(storjEvent::Error, "Download failed with error %s (%d)", storj_strerror(status), status);
	}
	else {
		fzprintf(storjEvent::Done);
	}
}

extern "C" void upload_file_complete(int status, char* file_id, void *)
{
	if (status) {
		fzprintf(storjEvent::Error, "Upload failed with error %s (%d)", storj_strerror(status), status);
	}
	else {
		fzprintf(storjEvent::Done);
	}
	free(file_id);
}

extern "C" void log(char const* msg, int level, void*)
{
	std::string s(msg);
	fz::replace_substrings(s, "\n", " ");
	fz::trim(s);
	fzprintf(storjEvent::Verbose, "%s", s);
}

extern "C" void generic_done(uv_work_t *work_req, int status)
{
	if (status) {
		fzprintf(storjEvent::Error, "Command failed with error %s (%d)", storj_strerror(status), status);
	}
	else {
		json_request_t *req = static_cast<json_request_t *>(work_req->data);

		if (req->status_code < 200 || req->status_code > 299) {
			fzprintf(storjEvent::Error, "Request failed with status code %d", req->status_code);
		}
		else {
			fzprintf(storjEvent::Done);
		}
	}
}

extern "C" void create_bucket_callback(uv_work_t *work_req, int status)
{
	create_bucket_request_t *req = static_cast<create_bucket_request_t *>(work_req->data);
	if (status) {
		fzprintf(storjEvent::Error, "Command failed with error %s (%d)", storj_strerror(status), status);
	}
	else {

		if (req->status_code == 404) {
			fzprintf(storjEvent::Error, "Cannot create bucket \"%s\": Name already exists", req->bucket->name);
		}
		else if (req->status_code != 201) {
			fzprintf(storjEvent::Error, "Request failed with status code %d", req->status_code);
		}
		else {
			fzprintf(storjEvent::Done);
		}

	}

	if (req) {
		json_object_put(req->response);
		free(req);
	}
	free(work_req);
}

extern "C" void delete_bucket_callback(uv_work_t *work_req, int status)
{
	json_request_t *req = static_cast<json_request_t *>(work_req->data);

	if (status) {
		fzprintf(storjEvent::Error, "Command failed with error %s (%d)", storj_strerror(status), status);
	}
	else {
		if (req->status_code != 200 && req->status_code != 204) {
			fzprintf(storjEvent::Error, "Request failed with status code %d", req->status_code);
		}
		else {
			fzprintf(storjEvent::Done);
		}

	}

	if (req) {
		json_object_put(req->response);
		free(req->path);
		free(req);
	}
	free(work_req);
}
}

int main()
{
	fzprintf(storjEvent::Reply, "fzStorj started, protocol_version=%d", FZSTORJ_PROTOCOL_VERSION);

	std::string host;
	unsigned short port = 443;
	std::string user;
	std::string pass;
	std::string mnemonic;
	std::string proxy;

	storj_env_t *env{};

	uint64_t timeout = STORJ_HTTP_TIMEOUT;

	auto init_env = [&](){
		if (env) {
			return;
		}
		storj_bridge_options_t options{};
		options.host = host.c_str();
		options.proto = "https";
		options.port = port;
		options.user = user.c_str();
		options.pass = pass.c_str();

		storj_encrypt_options_t encrypt_options{};
		encrypt_options.mnemonic = mnemonic.c_str();

		storj_http_options_t http_options{};
		http_options.user_agent = "FileZilla";
		http_options.low_speed_limit = STORJ_LOW_SPEED_LIMIT;
		http_options.low_speed_time = STORJ_LOW_SPEED_TIME;
		http_options.timeout = timeout;
		if (!proxy.empty()) {
			http_options.proxy_url = proxy.c_str();
		}

		static storj_log_options_t log_options{};
		log_options.logger = log;
		log_options.level = 4;
		env = storj_init_env(&options, &encrypt_options, &http_options, &log_options);
		if (!env) {
			fzprintf(storjEvent::Error, "storj_init_env failed");
			exit(1);
		}
	};

	int ret = 0;
	while (true) {
		std::string command;
		if (!getLine(command)) {
			ret = 1;
			break;
		}

		if (command.empty()) {
			break;
		}

		std::size_t pos = command.find(' ');
		std::string arg;
		if (pos != std::string::npos) {
			arg = command.substr(pos + 1);
			command = command.substr(0, pos);
		}

		if (command == "host") {
			host = arg;
			std::size_t sep = host.find(':');
			if (sep != std::string::npos) {
				port = fz::to_integral<unsigned short>(host.substr(pos + 1));
				host = host.substr(0, pos);
			}
			fzprintf(storjEvent::Done);
		}
		else if (command == "user") {
			user = arg;
			fzprintf(storjEvent::Done);
		}
		else if (command == "pass") {
			pass = arg;
			fzprintf(storjEvent::Done);
		}
		else if (command == "genkey") {

			char* buf = 0;
			storj_mnemonic_generate(128, &buf);
			if (buf) {
				fzprintf(storjEvent::Done, "%s", buf);
				free(buf);
			}
			else {
				fzprintf(storjEvent::Error);
			}
		}
		else if (command == "key" || command == "validatekey") {
			mnemonic = arg;
			if (storj_mnemonic_check(mnemonic.c_str())) {
				if (command == "key") {
					fzprintf(storjEvent::Done);
				}
				else {
					fzprintf(storjEvent::Done, "");
				}
			}
			else {
				fzprintf(storjEvent::Error, "Invalid encryption key");
			}
		}
		else if (command == "timeout") {
			timeout = fz::to_integral<uint64_t>(arg);
			fzprintf(storjEvent::Done);
		}
		else if (command == "proxy") {
			proxy = arg;
			fzprintf(storjEvent::Done);
		}
		else if (command == "list-buckets") {
			init_env();
			storj_bridge_get_buckets(env, 0, get_buckets_callback);
			if (uv_run(env->loop, UV_RUN_DEFAULT)) {
				fzprintf(storjEvent::Error, "uv_run failed.");
			}
		}
		else if (command == "list") {
			init_env();

			if (arg.empty()) {
				fzprintf(storjEvent::Error, "Bad arguments");
				continue;
			}

			std::string bucket, prefix;

			size_t pos = arg.find(' ');
			if (pos == std::string::npos) {
				bucket = arg;
			}
			else {
				bucket = arg.substr(0, pos);

				prefix = arg.substr(pos + 1);

				if (prefix.size() >= 2 && prefix.front() == '"' && prefix.back() == '"') {
					prefix = fz::replaced_substrings(prefix.substr(1, prefix.size() - 2), "\"\"", "\"");
				}

				if (!prefix.empty() && prefix.back() != '/') {
					fzprintf(storjEvent::Error, "Bad arguments");
					continue;
				}
			}

			storj_bridge_list_files(env, bucket.c_str(), &prefix, list_files_callback);
			if (uv_run(env->loop, UV_RUN_DEFAULT)) {
				fzprintf(storjEvent::Error, "uv_run failed.");
			}
		}
		else if (command == "get") {
			size_t pos = arg.find(' ');
			if (pos == std::string::npos) {
				fzprintf(storjEvent::Error, "Bad arguments");
				continue;
			}
			std::string bucket = arg.substr(0, pos);
			size_t pos2 = arg.find(' ', pos + 1);
			if (pos == std::string::npos) {
				fzprintf(storjEvent::Error, "Bad arguments");
				continue;
			}
			auto id = arg.substr(pos + 1, pos2 - pos - 1);
			auto file = arg.substr(pos2 + 1);

			if (file.size() >= 3 && file.front() == '"' && file.back() == '"') {
				file = fz::replaced_substrings(file.substr(1, file.size() - 2), "\"\"", "\"");
			}

			init_env();

			FILE *fd = fopen(file.c_str(), "w+");

			if (fd == NULL) {
				int err = errno;
				fzprintf(storjEvent::Error, "Could not open local file %s for writing: %d", file, err);
				continue;
			}

			storj_download_state_t *state = static_cast<storj_download_state_t*>(malloc(sizeof(storj_download_state_t)));

			/*uv_signal_t sig;
			uv_signal_init(env->loop, &sig);
			uv_signal_start(&sig, signal_handler, SIGINT);
			sig.data = state;*/

			uint64_t lastProgress{};
			int status = storj_bridge_resolve_file(env, state, bucket.c_str(),
												   id.c_str(), fd, &lastProgress,
												   download_file_progress,
												   download_file_complete);
			if (status) {
				fclose(fd);
				fzprintf(storjEvent::Error, "Could not download file, storj_bridge_resolve_file failed: %d", status);
				continue;
			}
			if (uv_run(env->loop, UV_RUN_DEFAULT)) {
				fclose(fd);
				fzprintf(storjEvent::Error, "uv_run failed.");
			}
			fclose(fd);
		}
		else if (command == "put") {
			std::string bucket = next_argument(arg);
			std::string file = next_argument(arg);
			std::string remote_name = next_argument(arg);

			if (bucket.empty() || file.empty() || remote_name.empty() || !arg.empty()) {
				fzprintf(storjEvent::Error, "Bad arguments");
			}

			init_env();

			FILE *fd{};
			if (file == "null") {
#ifdef FZ_WINDOWS
				char buf[MAX_PATH + 2];
				int ret = GetTempPathA(MAX_PATH + 1, buf);
				if (ret && ret <= MAX_PATH + 1) {
					char buf2[MAX_PATH + 1];
					ret = GetTempFileNameA(buf, "fzstorj", 0, buf2);
					if (ret) {
						fd = fopen(buf2, "r+D");
						if (fd) {
							fwrite(buf2, 1, 1, fd);
							rewind(fd);
						}
					}
				}
#else
				std::string tmpname = P_tmpdir;
				tmpname += "/fzstorjXXXXXX";
				char* buf = &tmpname[0];
				int f = mkstemp(buf);
				if (f != -1) {
					unlink(buf);
					write(f, buf, 1);
					lseek(f, 0, SEEK_SET);
					fd = fdopen(f, "r");
				}
#endif
			}
			else {
				fd = fopen(file.c_str(), "r");
			}

			if (fd == NULL) {
				int err = errno;
				fzprintf(storjEvent::Error, "Could not open local file %s for reading: %d", file, err);
				continue;
			}


			storj_upload_opts_t upload_opts;
			upload_opts.prepare_frame_limit = 1;
			upload_opts.push_frame_limit = 64;
			upload_opts.push_shard_limit = 64;
			upload_opts.bucket_id = bucket.c_str();
			upload_opts.file_name = remote_name.c_str();
			upload_opts.index = NULL;
			upload_opts.rs = true;
			upload_opts.fd = fd;

			storj_upload_state_t *state = static_cast<storj_upload_state_t*>(malloc(sizeof(storj_upload_state_t)));

			uint64_t lastProgress{};
			int status = storj_bridge_store_file(env,
												 state,
												 &upload_opts,
												 &lastProgress,
												 download_file_progress,
												 upload_file_complete);

			if (status) {
				fzprintf(storjEvent::Error, "Could not upload file, storj_bridge_store_file failed: %d", status);
				continue;
			}
			if (uv_run(env->loop, UV_RUN_DEFAULT)) {
				fzprintf(storjEvent::Error, "uv_run failed.");
			}
		}
		else if (command == "rm") {
			auto args = fz::strtok(arg, ' ');
			if (args.size() != 2) {
				fzprintf(storjEvent::Error, "Bad arguments");
				continue;
			}
			init_env();

			int status = storj_bridge_delete_file(env, args[0].c_str(), args[1].c_str(), nullptr, generic_done);

			if (status) {
				fzprintf(storjEvent::Error, "Could not delete file, storj_bridge_delete_file failed: %d", status);
				continue;
			}
			if (uv_run(env->loop, UV_RUN_DEFAULT)) {
				fzprintf(storjEvent::Error, "uv_run failed.");
			}
		}
		else if (command == "mkbucket") {
			std::string bucket = next_argument(arg);
			if (bucket.empty() || !arg.empty()) {
				fzprintf(storjEvent::Error, "Bad arguments");
				continue;
			}
			init_env();

			int status = storj_bridge_create_bucket(env, bucket.c_str(),
												   NULL, create_bucket_callback);

			if (status) {
				fzprintf(storjEvent::Error, "Could not create bucket, storj_bridge_create_bucket failed: %d", status);
				continue;
			}
			if (uv_run(env->loop, UV_RUN_DEFAULT)) {
				fzprintf(storjEvent::Error, "uv_run failed.");
			}
		}
		else if (command == "rmbucket") {
			auto args = fz::strtok(arg, ' ');
			if (args.size() != 1) {
				fzprintf(storjEvent::Error, "Bad arguments");
				continue;
			}
			init_env();

			int status = storj_bridge_delete_bucket(env, args.front().c_str(),
												   NULL, delete_bucket_callback);

			if (status) {
				fzprintf(storjEvent::Error, "Could not remove bucket, storj_bridge_delete_bucket failed: %d", status);
				continue;
			}
			if (uv_run(env->loop, UV_RUN_DEFAULT)) {
				fzprintf(storjEvent::Error, "uv_run failed.");
			}
		}
		else {
			fzprintf(storjEvent::Error, "No such command: %s", command);
		}

	}

	if (env) {
		storj_destroy_env(env);
	}

	return ret;
}
