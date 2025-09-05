#pragma once
#include <string>

class DaveyController {
public:
    // Unique key for this controller
    std::string key = "Davey_controller";

    DaveyController();
    void update(float dt);

private:
    // TODO: add state here
};
