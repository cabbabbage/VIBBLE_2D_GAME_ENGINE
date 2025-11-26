# Asks

# Asks

- Implement rotation for children assets in the main game rendering
- Extract light flicker application into its own class, removing it from the renderer class
- Add a dedicated vector in asset code for render_to_mask light objects (better named than 'light_to_rnder_to_mask') to improve renderer access without searching; populated by composite renderer alongside normal light textures
