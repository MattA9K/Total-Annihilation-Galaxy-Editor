#include "platform/UpdatePingInternal.hpp"

#include <curl/curl.h>

#include <mutex>
#include <string>
#include <sys/utsname.h>

namespace galaxy {
namespace {

std::once_flag curlReady;

std::size_t discardResponse(char*, std::size_t size, std::size_t count, void*) {
    return size * count;
}

}  // namespace

void postUpdateCheck(std::string_view body) {
    const std::string payload(body);
    std::call_once(curlReady, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        return;
    }
    curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, "https://starnexa.net/galaxy_editor_services/check_for_update/");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discardResponse);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}

std::string hostPlatformName() {
    return "Linux";
}

std::string hostOperatingSystem() {
    utsname info{};
    if (uname(&info) != 0) {
        return "Linux";
    }
    return std::string("Linux ") + info.release;
}

}  // namespace galaxy
