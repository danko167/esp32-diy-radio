// Internal to the upload server: what its core (upload_server.cpp) shares
// with the files that add a feature's routes (upload_*.cpp)
#pragma once

#include <Arduino.h>
#include <FS.h>

#include <vector>

#include "app/storage.h"
#include "net/bounded_server.h"
#include "net/stations.h"
#include "ui/wallpaper.h"
#include "upload_server.h"

namespace upload_server {

constexpr size_t kPictureUpload = wallpaper::kThumbBytes + wallpaper::kPictureBytes;

// ----- In upload_server.cpp -----

extern BoundedServer *server;

// Holds the mutex that guards what both tasks touch: the pending change,
// pictureIds, `current` and `rev`
struct Lock {
    Lock();
    ~Lock();
};

extern volatile uint32_t lastActivity;  // the last sign of the page using it, for secondsLeft()
extern uint32_t rev;                     // what revision() answers
extern Transfer current;                 // the picture on its way, for transfer()
extern volatile bool cancelRequested;    // the radio cut the upload, or the server is stopping
extern storage::CardUse rawCard;         // the card, for the raw upload being received

void noDelay();
bool codeOk(const String &given);
// From the query string: fine for requests without a raw body (see receivePicture)
bool authorized();
// A raw body refused at its start: answered and its connection closed
void refuseRaw(int status, const char *why);
// A card file as the answer (its CardUse held by the caller): streamFile(),
// but cut when the server stops, and activity all along
void sendFile(File &f, const char *type);

// A change the UI task makes, with what it needs set under a Lock first
enum class Action { None, AddPicture, DeletePicture, SetStations, SaveFace, SaveFaceAsset, FaceDone, DeleteFace };
extern uint16_t pendingId;
extern String pendingName;  // a picture's, or a face asset's file name
extern std::vector<stations::Station> pendingStations;
// Hands it to the UI task and waits for it to be done; false when it failed
bool request(Action action);

extern std::vector<uint16_t> pictureIds;  // the ids of the wallpapers after the designs, for thumbnails

// ----- In the features' files -----

// Each adds its routes to `server`, from start()
void addPictureRoutes();    // upload_pictures.cpp
void addStationRoutes();    // upload_stations.cpp
void addFaceRoutes();       // upload_faces.cpp
void addRecordingRoutes();  // upload_recordings.cpp

// Their part of apply(), on the UI task
void applyPicture(Action action, uint16_t id, const String &name);
bool applyFace(Action action, uint16_t id, const String &name);  // false when the card doesn't take a file

}  // namespace upload_server
