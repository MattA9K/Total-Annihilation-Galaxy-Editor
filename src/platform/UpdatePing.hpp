#pragma once

namespace galaxy {

// Contacts the update service on a background thread: once as the editor opens,
// then every 30 minutes it stays open. stopUpdateCheck waits for the current
// request to finish. The service's reply is not read.
void startUpdateCheck();
void stopUpdateCheck();

}  // namespace galaxy
