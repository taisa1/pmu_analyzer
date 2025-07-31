#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <string>
#include <sys/time.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>
#include <yaml-cpp/yaml.h>

#include <pmu_analyzer.hpp>

namespace pmu_analyzer {

struct LogEntry {
  int session_idx;
  int part_idx;
  int loop_idx;
  unsigned long long timestamp;
  long long data;
};
struct VarLogEntryVec {
  int session_idx;
  int var_idx;
  int loop_idx;
  std::vector<double> datavec;
};
struct VarLogStringVec {
  int session_idx;
  int var_idx;
  int loop_idx;
  std::vector<std::string> datavec;
};

static std::mutex mtx;

// Guarded - from here
static std::vector<std::string> session_names;
static std::vector<std::string> variable_names;
static std::unordered_map<std::string, int> session2idx;
static std::unordered_map<std::string, int> variable2idx;

static std::vector<std::vector<LogEntry>> logs;
static std::vector<std::vector<VarLogEntryVec>> var_logs;
static std::vector<std::vector<VarLogStringVec>> var_logs_string;

static std::vector<int> logs_num;
static std::vector<int> var_logs_num;
static std::vector<int> var_logs_string_num;

static std::vector<int> loop_idxs;
// Guarded - to here

static std::string log_path;
static int max_logs_num;

void ELAPSED_TIME_INIT(std::string &session_name) {
  static bool first(true); // Guarded
  static std::atomic<int> session_idx(0);

  int local_session_idx = session_idx++;

  {
    std::lock_guard<std::mutex> lock(mtx);

    if (first) {
      first = false;
      YAML::Node config =
          YAML::LoadFile(std::getenv("PMU_ANALYZER_CONFIG_FILE"));
      log_path = config["log_path"].as<std::string>();
      max_logs_num = config["max_logs_num"]["elapsed_time"].as<int>();
    }

    session2idx[session_name] = local_session_idx;
    session_names.push_back(session_name);

    logs.push_back(std::vector<LogEntry>());
    var_logs.push_back(std::vector<VarLogEntryVec>());
    var_logs_string.push_back(std::vector<VarLogStringVec>());
    logs[logs.size() - 1].resize(max_logs_num);
    var_logs[var_logs.size() - 1].resize(max_logs_num);
    var_logs_string[var_logs_string.size() - 1].resize(max_logs_num);
    for (int i = 0; i < max_logs_num; i++) { // memory touch
      volatile LogEntry tmp = logs[logs.size() - 1][i];
      volatile VarLogEntryVec vartmp = var_logs[var_logs.size() - 1][i];
      volatile VarLogStringVec varstrtmp =
          var_logs_string[var_logs_string.size() - 1][i];
    }

    logs_num.push_back(0);
    var_logs_num.push_back(0);
    var_logs_string_num.push_back(0);
    loop_idxs.push_back(0);
  }
}

void ELAPSED_TIME_TIMESTAMP(std::string &session_name, int part_idx,
                            bool new_loop, long long data) {
  int local_session_idx = session2idx[session_name];
  if (new_loop)
    loop_idxs[local_session_idx]++;

  LogEntry &e = logs[local_session_idx][logs_num[local_session_idx]];
  e.session_idx = local_session_idx;
  e.part_idx = part_idx;
  e.loop_idx = loop_idxs[local_session_idx];
  e.data = data;

  struct timeval tv;
  gettimeofday(&tv, NULL);
  e.timestamp = 1000 * 1000 * tv.tv_sec + tv.tv_usec;

  logs_num[local_session_idx]++;
}

void ELAPSED_TIME_CLOSE(std::string &session_name) {
  int local_session_idx = session2idx[session_name];

  char logfile_name[100], var_logfile_name[100];
  sprintf(logfile_name, "%s/elapsed_time_log_%d_%d", log_path.c_str(), getpid(),
          local_session_idx);
  sprintf(var_logfile_name, "%s/variables_log_%d_%d", log_path.c_str(),
          getpid(), local_session_idx);

  FILE *f = fopen(logfile_name, "a+");

  for (int i = 0; i < logs_num[local_session_idx]; i++) {
    LogEntry &e = logs[local_session_idx][i];
    fprintf(f, "%s %d %d %lld %lld\n", session_name.c_str(), e.part_idx,
            e.loop_idx, e.timestamp, e.data);
  }

  std::ofstream ofs(std::string(var_logfile_name), std::ios::app);
  for (int i = 0; i < var_logs_num[local_session_idx]; i++) {
    VarLogEntryVec &e = var_logs[local_session_idx][i];
    ofs << session_name << " " << e.loop_idx << " "
        << variable_names[e.var_idx];
    for (auto &d : e.datavec) {
      ofs << " " << d;
    }
    ofs << std::endl;
  }
  std::ofstream ofs_string(std::string(var_logfile_name) + "_string",
                           std::ios::app);
  for (int i = 0; i < var_logs_string_num[local_session_idx]; i++) {
    VarLogStringVec &e = var_logs_string[local_session_idx][i];
    ofs_string << session_name << " " << e.loop_idx << " "
               << variable_names[e.var_idx];
    for (auto &d : e.datavec) {
      ofs_string << " " << d;
    }
    ofs_string << std::endl;
  }

  fclose(f);
  ofs.close();
}

void VAR_LOG_SINGLE(const std::string &session_name,
                    const std::string &variable_name, const double &data) {
  int local_session_idx = session2idx[session_name];
  if (variable2idx.count(variable_name) == 0) {
    int new_id = variable2idx.size();
    variable2idx[variable_name] = new_id;
    variable_names.push_back(variable_name);
  }
  int local_var_idx = variable2idx[variable_name];

  VarLogEntryVec &e =
      var_logs[local_session_idx][var_logs_num[local_session_idx]];
  e.session_idx = local_session_idx;
  e.var_idx = local_var_idx;
  e.loop_idx = loop_idxs[local_session_idx];
  e.datavec.push_back(data);

  var_logs_num[local_session_idx]++;
}

void VAR_LOG_VEC(const std::string &session_name,
                 const std::string &variable_name,
                 const std::vector<double> &data) {
  int local_session_idx = session2idx[session_name];
  if (variable2idx.count(variable_name) == 0) {
    int new_id = variable2idx.size();
    variable2idx[variable_name] = new_id;
    variable_names.push_back(variable_name);
  }
  int local_var_idx = variable2idx[variable_name];

  VarLogEntryVec &e =
      var_logs[local_session_idx][var_logs_num[local_session_idx]];
  e.session_idx = local_session_idx;
  e.var_idx = local_var_idx;
  e.loop_idx = loop_idxs[local_session_idx];
  for (auto &d : data) {
    e.datavec.push_back(d);
  }
  var_logs_num[local_session_idx]++;
}

void VAR_LOG_STRING(const std::string &session_name,
                    const std::string &variable_name,
                    const std::vector<std::string> &data) {
  int local_session_idx = session2idx[session_name];
  if (variable2idx.count(variable_name) == 0) {
    int new_id = variable2idx.size();
    variable2idx[variable_name] = new_id;
    variable_names.push_back(variable_name);
  }
  int local_var_idx = variable2idx[variable_name];

  VarLogStringVec &e = var_logs_string[local_session_idx]
                                      [var_logs_string_num[local_session_idx]];
  e.session_idx = local_session_idx;
  e.var_idx = local_var_idx;
  e.loop_idx = loop_idxs[local_session_idx];
  for (auto &d : data) {
    e.datavec.push_back(d);
  }
  var_logs_string_num[local_session_idx]++;
}
} // namespace pmu_analyzer
