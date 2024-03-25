#include "config.h"
#include "il2cpp-api-types.h"

#include <il2cpp/il2cpp_helper.h>

#include <prime/EntityGroup.h>
#include <prime/HttpResponse.h>
#include <prime/Hub.h>
#include <prime/LanguageManager.h>
#include <prime/ServiceResponse.h>

#include <Digit.PrimeServer.Models.pb.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <EASTL/algorithm.h>
#include <EASTL/bonus/ring_buffer.h>

#if _WIN32
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.Headers.h>
#endif

#include <curl/curl.h>

#include <condition_variable>
#include <format>
#include <fstream>
#include <ostream>
#include <queue>
#include <string>

static std::string  to_string(const std::wstring& str);
static std::wstring to_wstring(const std::string& str);

namespace http
{

struct CURLClient {
  CURLClient(CURL* handle)
      : handle_(handle)
  {
  }

  operator CURL*() const
  {
    return this->handle_;
  }

  ~CURLClient()
  {
    curl_easy_cleanup(handle_);
  }

private:
  CURL* handle_;
};

static void send_data(std::wstring post_data)
{
  if (Config::Get().sync_url.empty()) {
    return;
  }

  std::wstring httpResponseBody;
  CURL*        httpClient = curl_easy_init();

  curl_easy_setopt(httpClient, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
  curl_easy_setopt(httpClient, CURLOPT_USERAGENT, "stfc community patch");

  struct curl_slist* list = NULL;

  list = curl_slist_append(list, "Content-Type: application/json");

  auto token             = Config::Get().sync_token;
  auto sync_token_header = "stfc-sync-token: " + token;
  if (!token.empty()) {
    list = curl_slist_append(list, sync_token_header.c_str());
  }

  if (list) {
    curl_easy_setopt(httpClient, CURLOPT_HTTPHEADER, list);
  }

  auto url = Config::Get().sync_url;
  if (auto r = curl_easy_setopt(httpClient, CURLOPT_URL, url.c_str()); r != CURLE_OK) {
    auto text = "Curl failed with: " + std::to_string((int)r);
    text      = text;
    throw std::runtime_error("Failed to send data");
  }

  auto post_data_str = to_string(post_data);
  if (curl_easy_setopt(httpClient, CURLOPT_POSTFIELDS, post_data_str.c_str()) != CURLE_OK) {
    throw std::runtime_error("Failed to send data");
  }

  auto res = curl_easy_perform(httpClient);
  if (res != CURLE_OK) {
    throw std::runtime_error("Failed to send data");
  }
}

static size_t curl_write_to_string(void* contents, size_t size, size_t nmemb, std::string* s)
{
  size_t newLength = size * nmemb;
  s->append((char*)contents, newLength);
  return newLength;
}

static std::wstring get_data_data(std::wstring session, std::wstring url, std::wstring path, std::wstring post_data)
{
  if (Config::Get().sync_url.empty()) {
    return {};
  }

  CURL* httpClient = curl_easy_init();

  curl_easy_setopt(httpClient, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
  curl_easy_setopt(httpClient, CURLOPT_USERAGENT, "stfc community patch");

  struct curl_slist* list = NULL;

  list = curl_slist_append(list, "Content-Type: application/json");

  auto token             = Config::Get().sync_token;
  auto sync_token_header = "stfc-sync-token: " + token;
  if (!token.empty() && session.empty()) {
    list = curl_slist_append(list, sync_token_header.c_str());
  }

  auto session_id_header = "X-AUTH-SESSION-ID: " + to_string(session);
  if (!session.empty()) {
    list = curl_slist_append(list, session_id_header.c_str());
    list = curl_slist_append(list, "X-PRIME-VERSION: meh");
    list = curl_slist_append(list, "X-Api-Key: meh");
    list = curl_slist_append(list, "X-PRIME-SYNC: 0");
  }

  if (list) {
    curl_easy_setopt(httpClient, CURLOPT_HTTPHEADER, list);
  }

  if (url.ends_with(L"/")) {
    url = url.substr(0, url.length() - 1);
    url += path;
  } else {
    url += path;
  }

  curl_easy_setopt(httpClient, CURLOPT_URL, to_string(url).c_str());
  curl_easy_setopt(httpClient, CURLOPT_POSTFIELDS, to_string(post_data).c_str());

  std::string s;

  curl_easy_setopt(httpClient, CURLOPT_WRITEFUNCTION, curl_write_to_string);
  curl_easy_setopt(httpClient, CURLOPT_WRITEDATA, &s);

  auto res = curl_easy_perform(httpClient);
  if (res != CURLE_OK) {
    throw std::runtime_error("Failed to send data");
  }

  long http_code = 0;
  curl_easy_getinfo(httpClient, CURLINFO_RESPONSE_CODE, &http_code);
  if (http_code != 200) {
    throw std::runtime_error("Failed to send data");
  }

  return to_wstring(s);

  return {};
}

static void send_data(std::string post_data)
{
  return send_data(to_wstring(post_data));
}

static void write_data(std::string file_data)
{
  if (!Config::Get().sync_file.empty()) {
    std::ofstream sync_file;
    sync_file.open(Config::Get().sync_file, std::ios_base::app);

    sync_file << file_data << "\n\n";
    sync_file.close();
  }
}

} // namespace http

#include <simdutf.h>

static std::wstring to_wstring(const std::string& str)
{
#if _WIN32
  return winrt::to_hstring(str).c_str();
#else
  size_t                      expected_utf32words = simdutf::utf32_length_from_utf8(str.data(), str.length());
  std::unique_ptr<char32_t[]> utf32_output{new char32_t[expected_utf32words]};
  size_t                      utf16words = simdutf::convert_utf8_to_utf32(str.data(), str.length(), utf32_output.get());
  return std::wstring(utf32_output.get(), utf32_output.get() + utf16words);
#endif
}

static std::wstring to_wstring(Il2CppString* str)
{
#if _WIN32
  return str->chars;
#else
  size_t                      expected_utf32words = simdutf::utf32_length_from_utf16(str->chars, str->length);
  std::unique_ptr<char32_t[]> utf32_output{new char32_t[expected_utf32words]};
  size_t                      utf16words = simdutf::convert_utf16_to_utf32(str->chars, str->length, utf32_output.get());
  return std::wstring(utf32_output.get(), utf32_output.get() + utf16words);
#endif
}

static std::string to_string(const std::wstring& str)
{
#if _WIN32
  size_t                     expected_utf16words = simdutf::utf8_length_from_utf16((char16_t*)str.data(), str.length());
  std::unique_ptr<char8_t[]> utf8_output{new char8_t[expected_utf16words]};
  size_t utf16words = simdutf::convert_utf16_to_utf8((char16_t*)str.data(), str.length(), (char*)utf8_output.get());
  return std::string(utf8_output.get(), utf8_output.get() + utf16words);
#else
  size_t                     expected_utf32words = simdutf::utf8_length_from_utf32((char32_t*)str.data(), str.length());
  std::unique_ptr<char8_t[]> utf32_output{new char8_t[expected_utf32words]};
  size_t utf16words = simdutf::convert_utf32_to_utf8((char32_t*)str.data(), str.length(), (char*)utf32_output.get());
  return std::string(utf32_output.get(), utf32_output.get() + utf16words);
#endif
}

std::mutex              m;
std::condition_variable cv;
std::queue<std::string> sync_data_queue;

std::mutex              m2;
std::condition_variable cv2;
std::queue<uint64_t>    combat_log_data_queue;

void queue_data(std::string data)
{
  if (data == "[]")
    return;

  {
    std::lock_guard lk(m);
    sync_data_queue.push(data);
  }
  cv.notify_all();
}

void HandleEntityGroup(EntityGroup* entity_group);

void MissionsDataContainer_ParseBinaryObject(auto original, void* _this, EntityGroup* group, bool isPlayerData)
{
  HandleEntityGroup(group);
  return original(_this, group, isPlayerData);
}

void GameServerModelRegistry_ProcessResultInternal(auto original, void* _this, HttpResponse* http_response,
                                                   ServiceResponse* service_response, void* callback,
                                                   void* callback_error)
{
  auto entity_groups = service_response->EntityGroups;
  for (int i = 0; i < entity_groups->Count; ++i) {
    auto entity_group = entity_groups->get_Item(i);
    HandleEntityGroup(entity_group);
  }

  return original(_this, http_response, service_response, callback, callback_error);
}
void GameServerModelRegistry_HandleBinaryObjects(auto original, void* _this, ServiceResponse* service_response)
{
  auto entity_groups = service_response->EntityGroups;
  for (int i = 0; i < entity_groups->Count; ++i) {
    auto entity_group = entity_groups->get_Item(i);
    HandleEntityGroup(entity_group);
  }

  return original(_this, service_response);
}

struct ResourceState {
  ResourceState(int64_t amount = -1)
      : amount(amount)
  {
  }

  inline operator int64_t() const
  {
    return amount;
  }

private:
  int64_t amount = -1;
};

struct RankLevelState {
  RankLevelState(int64_t a = -1, int64_t b = -1)
      : a(a)
      , b(b)
  {
  }

  bool operator==(const RankLevelState& other) const
  {
    return this->a == other.a && this->b == other.b;
  }

private:
  int64_t a = -1;
  int64_t b = -1;
};

struct pairhash {
public:
  template <typename T, typename U> std::size_t operator()(const std::pair<T, U>& x) const
  {
    return std::hash<T>()(x.first) ^ std::hash<U>()(x.second);
  }
};

static std::unordered_map<uint64_t, ResourceState>                               resource_states;
static std::unordered_map<uint64_t, ResourceState>                               module_states;
static std::unordered_map<uint64_t, RankLevelState>                              officer_states;
static std::unordered_map<uint64_t, RankLevelState>                              ft_states;
static std::unordered_map<std::pair<int64_t, int64_t>, RankLevelState, pairhash> trait_states;
static std::unordered_set<int64_t>                                               mission_states;
static std::unordered_set<int64_t>                                               active_mission_states;
static std::unordered_set<uint64_t>                                              battlelog_states;

static eastl::ring_buffer<uint64_t> previously_sent_battlelogs;

static void load_previously_sent_logs()
{
  previously_sent_battlelogs.set_capacity(300);
  using json = nlohmann::json;
  try {
    std::ifstream file("patch_battlelogs_sent.json");
    std::string   battlelog_json;
    file >> battlelog_json;
    const auto battlelogs = json::parse(battlelog_json);
    for (auto v : battlelogs) {
      previously_sent_battlelogs.push_back(v.get<uint64_t>());
    }
  } catch (...) {
  }
}

static void save_previously_sent_logs()
{
  using json           = nlohmann::json;
  auto battlelog_array = json::array();
  for (auto id : previously_sent_battlelogs) {
    battlelog_array.push_back(id);
  }
  std::ofstream file("patch_battlelogs_sent.json");
  file << battlelog_array.dump();
  file.close();
}

void HandleEntityGroup(EntityGroup* entity_group)
{
  using json = nlohmann::json;

  auto bytes = entity_group->Group;
  auto type  = entity_group->Type_;
  if (type == EntityGroup::Type::ActiveMissions) {
    auto response = Digit::PrimeServer::Models::ActiveMissionsResponse();
    if (response.ParseFromArray(bytes->bytes->m_Items, bytes->bytes->max_length)) {
      auto mission_array = json::array();
      for (const auto& mission : response.activemissions()) {
        mission_array.push_back({{"type", "active_mission"}, {"mid", mission.id()}});
      }
      if (Config::Get().sync_missions) {
        queue_data(mission_array.dump());
      }
    }
  } else if (type == EntityGroup::Type::CompletedMissions) {
    auto response = Digit::PrimeServer::Models::CompletedMissionsResponse();
    if (response.ParseFromArray(bytes->bytes->m_Items, bytes->bytes->max_length)) {
      auto mission_array = json::array();
      for (const auto& mission : response.completedmissions()) {
        if (!mission_states.contains(mission)) {
          mission_states.insert(mission);
          mission_array.push_back({{"type", "mission"}, {"mid", mission}});
        }
      }
      if (Config::Get().sync_missions) {
        queue_data(mission_array.dump());
      }
    }
  } else if (type == EntityGroup::Type::PlayerInventories) {
    auto response = Digit::PrimeServer::Models::InventoryResponse();
    if (response.ParseFromArray(bytes->bytes->m_Items, bytes->bytes->max_length)) {
      auto inventory_object = json();
      for (const auto& inventory : response.inventories()) {
        for (const auto& item : inventory.second.items()) {
          auto type = item.type();
          if (type == Digit::PrimeServer::Models::INVENTORYITEMTYPE_INVENTORYRESOURCE)
            inventory_object[std::to_string(item.commonparams().refid())] = item.count();
        }
      }
    }
  } else if (type == EntityGroup::Type::ResearchTreesState) {
    auto response = Digit::PrimeServer::Models::ResearchTreesState();
    if (response.ParseFromArray(bytes->bytes->m_Items, bytes->bytes->max_length)) {
      auto research_array = json::array();
      for (const auto& research : response.researchprojectlevels()) {
        research_array.push_back({{"type", "research"}, {"rid", research.first}, {"level", research.second}});
      }
      if (Config::Get().sync_research) {
        queue_data(research_array.dump());
      }
    }
  } else if (type == EntityGroup::Type::Officers) {
    auto response = Digit::PrimeServer::Models::OfficersResponse();
    if (response.ParseFromArray(bytes->bytes->m_Items, bytes->bytes->max_length)) {
      auto officers_array = json::array();
      for (const auto& officer : response.officers()) {
        if (officer.rankindex() == 0) {
          continue;
        }
        if (officer_states[officer.id()] != RankLevelState{officer.rankindex(), officer.level()}) {
          officer_states[officer.id()] = RankLevelState{officer.rankindex(), officer.level()};
          officers_array.push_back(
              {{"type", "officer"}, {"oid", officer.id()}, {"rank", officer.rankindex()}, {"level", officer.level()}});
        }
      }
      if (Config::Get().sync_officer) {
        queue_data(officers_array.dump());
      }
    }
  } else if (type == EntityGroup::Type::ForbiddenTechs) {
    auto response = Digit::PrimeServer::Models::ForbiddenTechsResponse();
    if (response.ParseFromArray(bytes->bytes->m_Items, bytes->bytes->max_length)) {
      auto ft_array = json::array();
      for (const auto& ft : response.forbiddentechs()) {
        if (ft_states[ft.id()] != RankLevelState{ft.tier(), ft.level()}) {
          ft_states[ft.id()] = RankLevelState{ft.tier(), ft.level()};
          ft_array.push_back({{"type", "ft"}, {"fid", ft.id()}, {"tier", ft.tier()}, {"level", ft.level()}});
        }
      }
      if (Config::Get().sync_tech) {
        queue_data(ft_array.dump());
      }
    }
  } else if (type == EntityGroup::Type::ActiveOfficerTraits) {
    auto response = Digit::PrimeServer::Models::OfficerTraitsResponse();
    if (response.ParseFromArray(bytes->bytes->m_Items, bytes->bytes->max_length)) {
      auto trait_array = json::array();
      for (const auto& trait_outer : response.activeofficertraits()) {
        for (const auto& trait : trait_outer.second.activetraits()) {
          const auto key = std::make_pair(trait_outer.first, trait.first);
          if (trait_states[key] != trait.second.level()) {
            trait_states[key] = trait.second.level();
            trait_array.push_back(
                {{"type", "trait"}, {"oid", trait_outer.first}, {"tid", trait.first}, {"level", trait.second.level()}});
          }
        }
      }
      if (Config::Get().sync_traits) {
        queue_data(trait_array.dump());
      }
    }
  } else if (type == EntityGroup::Type::Json) {
    try {
      using json = nlohmann::json;

      auto text   = bytes->bytes->m_Items;
      auto result = json::parse(text);

      if (result.contains("battle_result_headers")) {
        auto headers = result["battle_result_headers"];
        if (Config::Get().sync_battlelogs && battlelog_states.empty()) {
          load_previously_sent_logs();
          for (const auto header : headers) {
            const auto id = header["id"].get<uint64_t>();
            battlelog_states.insert(id);
            if (eastl::find(previously_sent_battlelogs.begin(), previously_sent_battlelogs.end(), id)
                == previously_sent_battlelogs.end()) {
              combat_log_data_queue.push(id);
              previously_sent_battlelogs.push_back(id);
            }
          }
          save_previously_sent_logs();
        } else if (Config::Get().sync_battlelogs) {
          std::lock_guard lk(m2);
          for (const auto header : headers) {
            const auto id = header["id"].get<uint64_t>();
            if (!battlelog_states.contains(id)) {
              battlelog_states.insert(id);
              combat_log_data_queue.push(id);
              previously_sent_battlelogs.push_back(id);
              save_previously_sent_logs();
            }
          }
        }
        cv2.notify_all();
      }

      if (result.contains("resources")) {
        auto resource_array = json::array();
        for (const auto& resource : result["resources"].get<json::object_t>()) {
          auto id     = std::stoll(resource.first);
          auto amount = resource.second["current_amount"].get<int64_t>();
          if (amount == 0) {
            continue;
          }
          if (resource_states[id] != amount) {
            resource_states[id] = amount;
            resource_array.push_back({{"type", "resource"}, {"rid", id}, {"amount", amount}});
          }
        }
        if (Config::Get().sync_resources) {
          queue_data(resource_array.dump());
        }
      }
      if (result.contains("starbase_modules")) {
        auto starbase_array = json::array();
        for (const auto& resource : result["starbase_modules"].get<json::object_t>()) {
          auto id    = resource.second["id"].get<uint64_t>();
          auto level = resource.second["level"].get<int64_t>();
          if (module_states[id] != level) {
            module_states[id] = level;
            starbase_array.push_back({{"type", "module"}, {"bid", id}, {"level", level}});
          }
        }
        if (Config::Get().sync_buildings) {
          queue_data(starbase_array.dump());
        }
      }
      if (result.contains("ships")) {
        auto ship_array = json::array();
        for (const auto& resource : result["ships"].get<json::object_t>()) {
          ship_array.push_back({{"type", "ship"},
                                {"psid", resource.second["id"]},
                                {"level", resource.second["level"]},
                                {"tier", resource.second["tier"]},
                                {"hull_id", resource.second["hull_id"]},
                                {"components", resource.second["components"]}});
        }
        if (Config::Get().sync_ships) {
          queue_data(ship_array.dump());
        }
      }
    } catch (json::exception e) {
    }
  }
}

static std::wstring instanceSessionId;
static std::wstring gameServerUrl;

void ship_sync_data()
{
#if _WIN32
  winrt::init_apartment();
#endif

  for (;;) {
    {
      std::unique_lock lk(m);
      cv.wait(lk, []() { return !sync_data_queue.empty(); });
    }
    const auto sync_data = ([&] {
      std::lock_guard lk(m);
      auto            data = sync_data_queue.front();
      sync_data_queue.pop();
      return data;
    })();
    try {
      http::send_data(sync_data);
#if _WIN32
    } catch (winrt::hresult_error const& ex) {
      spdlog::error("Failed to send sync data: {}", winrt::to_string(ex.message()).c_str());
    } catch (const std::wstring& sz) {
      spdlog::error("Failed to send sync data: {}", winrt::to_string(sz).c_str());
    }
#else
    } catch (const std::wstring& sz) {
      spdlog::error("Failed to send sync data: {}", to_string(sz));
    }
#endif
    catch (const std::runtime_error& e) {
      spdlog::error("Failed to send sync data: {}", e.what());
    }
  }
#if _WIN32
  winrt::uninit_apartment();
#endif
}

void ship_combat_log_data()
{
#if _WIN32
  winrt::init_apartment();
#endif

  for (;;) {
    {
      std::unique_lock lk(m2);
      cv2.wait(lk, []() { return !combat_log_data_queue.empty(); });
    }

    try {
      const auto sync_data = ([&] {
        std::lock_guard lk(m2);
        auto            data = combat_log_data_queue.front();
        combat_log_data_queue.pop();
        return data;
      })();

      auto body       = L"{{\"journal_id\":" + std::to_wstring(sync_data) + L"}}";
      auto battle_log = http::get_data_data(instanceSessionId, gameServerUrl, L"/journals/get", body);

      using json = nlohmann::json;

      auto ship_array  = json::array();
      auto battle_json = json::parse(battle_log);

      const auto journal = battle_json["journal"];

      const auto target_fleet_data    = journal["target_fleet_data"];
      const auto initiator_fleet_data = journal["initiator_fleet_data"];

      std::vector<std::string> profiles_to_fetch;

      if (target_fleet_data["ref_ids"].is_null()) {
        for (const auto& fleet : target_fleet_data["deployed_fleets"]) {
          const auto player_id = fleet["uid"].get<std::string>();
          profiles_to_fetch.push_back(std::string("\"") + player_id + "\"");
        }
      }

      if (initiator_fleet_data["ref_ids"].is_null()) {
        for (const auto& fleet : initiator_fleet_data["deployed_fleets"]) {
          const auto player_id = fleet["uid"].get<std::string>();
          profiles_to_fetch.push_back(std::string("\"") + player_id + "\"");
        }
      }

      std::string profiles_joined = std::accumulate(profiles_to_fetch.begin(), profiles_to_fetch.end(), std::string(),
                                                    [](const std::string& a, const std::string& b) -> std::string {
                                                      return a + (a.length() > 0 ? "," : "") + b;
                                                    });
      auto        profiles_body   = "{{\"user_ids\":[" + profiles_joined + "]}}";
      auto        profiles =
          http::get_data_data(instanceSessionId, gameServerUrl, L"/user_profile/profiles", to_wstring(profiles_body));
      auto profiles_json = json::parse(profiles);
      auto names         = json::object();
      for (const auto& profile : profiles_json["user_profiles"].get<json::object_t>()) {
        names[profile.first] = profile.second["name"];
      }
      ship_array.push_back({{"type", "battlelog"}, {"names", names}, {"journal", battle_json["journal"]}});

      try {
        auto ship_data = ship_array.dump();
        http::write_data(ship_data);
        http::send_data(ship_data);
#if _WIN32
      } catch (winrt::hresult_error const& ex) {
        spdlog::error("Failed to send sync data: {}", winrt::to_string(ex.message()).c_str());
      } catch (const std::wstring& sz) {
        spdlog::error("Failed to send sync data: {}", winrt::to_string(sz).c_str());
      }
#else
      } catch (const std::wstring& sz) {
        spdlog::error("Failed to send sync data: {}", to_string(sz));
      }
#endif
      catch (const std::runtime_error& e) {
        spdlog::error("Failed to send sync data: {}", e.what());
      }
    } catch (...) {
    }
  }

#if _WIN32
  winrt::uninit_apartment();
#endif
}

void PrimeApp_InitPrimeServer(auto original, void* _this, Il2CppString* gameServerUrl, Il2CppString* gatewayServerUrl,
                              Il2CppString* sessionId)
{
  original(_this, gameServerUrl, gatewayServerUrl, sessionId);
  ::instanceSessionId = to_wstring(sessionId);
  ::gameServerUrl     = to_wstring(gameServerUrl);
}

void InstallSyncPatches()
{
  curl_global_init(CURL_GLOBAL_ALL);

  auto missions_data_container =
      il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "MissionsDataContainer");
  auto ptr = missions_data_container.GetMethod("ParseBinaryObject");
  SPUD_STATIC_DETOUR(ptr, MissionsDataContainer_ParseBinaryObject);

  auto inventory_data_container =
      il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Services", "InventoryDataContainer");
  ptr = inventory_data_container.GetMethod("ParseBinaryObject");
  SPUD_STATIC_DETOUR(ptr, MissionsDataContainer_ParseBinaryObject);

  auto research_data_container =
      il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Services", "ResearchDataContainer");
  ptr = research_data_container.GetMethod("ParseBinaryObject");
  SPUD_STATIC_DETOUR(ptr, MissionsDataContainer_ParseBinaryObject);

  auto research_service =
      il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Services", "ResearchService");
  ptr = research_service.GetMethod("ParseBinaryObject");
  SPUD_STATIC_DETOUR(ptr, MissionsDataContainer_ParseBinaryObject);

  auto game_server_model_registry =
      il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Core", "GameServerModelRegistry");
  ptr = game_server_model_registry.GetMethod("ProcessResultInternal");
  SPUD_STATIC_DETOUR(ptr, GameServerModelRegistry_ProcessResultInternal);
  ptr = game_server_model_registry.GetMethod("HandleBinaryObjects");
  SPUD_STATIC_DETOUR(ptr, GameServerModelRegistry_HandleBinaryObjects);

  auto platform_model_registry =
      il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimePlatform.Core", "PlatformModelRegistry");
  ptr = platform_model_registry.GetMethod("ProcessResultInternal");
  SPUD_STATIC_DETOUR(ptr, GameServerModelRegistry_ProcessResultInternal);

  auto authentication_service = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.Core", "PrimeApp");
  ptr                         = authentication_service.GetMethod("InitPrimeServer");
  SPUD_STATIC_DETOUR(ptr, PrimeApp_InitPrimeServer);

  std::thread(ship_sync_data).detach();
  std::thread(ship_combat_log_data).detach();
}