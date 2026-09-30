//
// Created by tarek on 9/30/26.
//

#include "dbus_handler.h"

DBusHandler::DBusHandler(int id, const std::unique_ptr<sdbus::IConnection> &connection, TunerHandler *tuner_handler) {
    this->tuner_handler = tuner_handler;

    object = sdbus::createObject(
        *connection,
        sdbus::ObjectPath{std::format("/de/multilan/dabdaemon/tuner{}", id)}
    );

    object->addVTable(



    ).forInterface("de.multilan.dabdaemon.Tuner1");
}

DBusHandler::~DBusHandler() {
    object.reset();
}
