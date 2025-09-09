#pragma once
#include <string>

class Asset;

class AnimationManager {

	public:
    explicit AnimationManager(Asset* owner);
    ~AnimationManager();
    void update();

        private:
    Asset* self_ = nullptr;
};
