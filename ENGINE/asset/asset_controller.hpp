#ifndef ASSET_ASSET_CONTROLLER_HPP
#define ASSET_ASSET_CONTROLLER_HPP

class Input;

class AssetController {
 public:
  AssetController();
   virtual ~AssetController();

   virtual void update(const Input& in) = 0;
};

#endif
