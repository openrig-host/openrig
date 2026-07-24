#include "AssetLoader.h"
#include "BinaryData.h"
#include "Logger.h"

AssetLoader &AssetLoader::getInstance() {
  static AssetLoader instance;
  return instance;
}

std::unique_ptr<juce::Drawable>
AssetLoader::loadSVG(const juce::String &resourceName) {
  juce::ScopedLock sl(cacheLock);

  // Check cache first
  auto it = assetCache.find(resourceName);
  if (it != assetCache.end() && it->second != nullptr)
    return it->second->createCopy();

  // Try embedded BinaryData (standalone exe, no external files needed)
  int dataSize = 0;
  const char* data = BinaryData::getNamedResource(resourceName.toRawUTF8(), dataSize);
  if (data != nullptr && dataSize > 0) {
    auto svgXml = juce::XmlDocument::parse(juce::String::fromUTF8(data, dataSize));
    if (svgXml) {
      auto drawable = juce::Drawable::createFromSVG(*svgXml);
      if (drawable) {
        assetCache[resourceName] = std::move(drawable);
        LOG_INFO("Loaded asset from binary: " + resourceName);
        return assetCache[resourceName]->createCopy();
      }
    }
  }

  // Fallback: try file system (development mode)
  auto assetsDir = getAssetsDirectory();
  auto assetFile = assetsDir.getChildFile(resourceName);
  if (assetFile.existsAsFile()) {
    auto drawable = loadSVGFromFile(assetFile);
    if (drawable != nullptr) {
      assetCache[resourceName] = std::move(drawable);
      LOG_INFO("Loaded asset from file: " + resourceName);
      return assetCache[resourceName]->createCopy();
    }
  }

  LOG_ERROR("Failed to load asset: " + resourceName);
  return nullptr;
}

std::unique_ptr<juce::Drawable>
AssetLoader::loadSVGFromFile(const juce::File &file) {
  if (!file.existsAsFile())
    return nullptr;

  auto svgXml = juce::XmlDocument::parse(file);
  if (!svgXml)
    return nullptr;

  return juce::Drawable::createFromSVG(*svgXml);
}

void AssetLoader::preloadAssets() {
  LOG_INFO("Preloading skeuomorphic assets...");

  // Pre-load all existing SVG assets
  const juce::String assetsToLoad[] = {"volume_knob.svg", "toggle_switch.svg",
                                       "toggle_orange.svg", "vu_meter.svg"};

  int loadedCount = 0;
  for (const auto &asset : assetsToLoad) {
    if (loadSVG(asset) != nullptr) {
      loadedCount++;
    }
  }

  LOG_INFO("Preloaded " + juce::String(loadedCount) + " assets");
}

std::unique_ptr<juce::Drawable>
AssetLoader::getCachedAsset(const juce::String &name) {
  juce::ScopedLock sl(cacheLock);
  auto it = assetCache.find(name);
  if (it != assetCache.end() && it->second != nullptr) {
    return it->second->createCopy();
  }
  return nullptr;
}

bool AssetLoader::isAssetCached(const juce::String &name) const {
  juce::ScopedLock sl(cacheLock);
  return assetCache.find(name) != assetCache.end();
}

void AssetLoader::clearCache() {
  juce::ScopedLock sl(cacheLock);
  assetCache.clear();
  LOG_INFO("Asset cache cleared");
}

juce::File AssetLoader::getAssetsDirectory() const {
  // Resolve from the executable's location up to the project root,
  // then into Source/Resources/assets. Works regardless of CWD.
  auto exeDir = juce::File::getSpecialLocation(
                    juce::File::currentExecutableFile)
                    .getParentDirectory();
  // exeDir = Builds/VisualStudio2026/x64/Debug/App/
  // projectRoot = DaveCoreProject/ (5 levels up)
  auto projectRoot = exeDir.getParentDirectory()  // Debug
                         .getParentDirectory()     // x64
                         .getParentDirectory()     // VisualStudio2026
                         .getParentDirectory()     // Builds
                         .getParentDirectory();    // DaveCoreProject
  return projectRoot.getChildFile("Source")
      .getChildFile("Resources")
      .getChildFile("assets");
}
