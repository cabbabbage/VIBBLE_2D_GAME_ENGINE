#include <vector>
#include <string>

class entity {
    public: 
        enum Type{
            PLAYER,
            ENEMY,
            RANGEDENEMY
        };

    virtual int getHealth() = 0;

    private:
        int health;
        int speed;
        Type type;
        bool can_damage = true;
        std::vector<std::string> items;
};