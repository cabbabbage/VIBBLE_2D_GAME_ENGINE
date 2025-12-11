class AIController {
    public:
        enum State {
            IDLE,
            DAMAGED,
            ATTACK,
            DASH,
            PARRY,
            DEATH,
            PATROL
};

    void update(float time);
    void setState(State state);
};