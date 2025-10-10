#include "dev_controls_persistence.hpp"

#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

namespace devmode {

bool write_map_info_json(const std::string& path,
                         const nlohmann::json& data,
                         std::ostream& log) {
    if (path.empty()) {
        log << "[DevControls] Map info path is empty; cannot persist data\n";
        return false;
    }
    try {
        std::ofstream out(path);
        if (!out.is_open()) {
            log << "[DevControls] Failed to open '" << path << "' for writing\n";
            return false;
        }
        try {
            out << data.dump(2);
        } catch (const std::exception& ex) {
            log << "[DevControls] Failed to serialize map info JSON: " << ex.what() << "\n";
            return false;
        }
        if (!out.good()) {
            log << "[DevControls] Stream error while writing '" << path << "'\n";
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        log << "[DevControls] Exception writing map info: " << ex.what() << "\n";
        return false;
    } catch (...) {
        log << "[DevControls] Unknown exception writing map info\n";
        return false;
    }
}

}
