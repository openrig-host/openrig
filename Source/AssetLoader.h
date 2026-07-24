#pragma once

#include <JuceHeader.h>
#include <memory>
#include <unordered_map>


/**
 * AssetLoader
 *
 * Utility class for loading and caching SVG assets to improve performance.
 * Assets are loaded once at startup and reused throughout the application.
 *
 * Located in: davecore/DaveCoreProject/Source/Resources/assets/
 */
class AssetLoader {
public:
  /**
   * Get singleton instance of AssetLoader
   */
  static AssetLoader &getInstance();

  /**
   * Load an SVG asset from the resources directory
   *
   * @param resourceName Name of the SVG file (e.g., "volume_knob.svg")
   * @return Drawable object or nullptr if loading fails
   */
  std::unique_ptr<juce::Drawable> loadSVG(const juce::String &resourceName);

  /**
   * Pre-load and cache all common assets at startup
   * Call this once during application initialization
   */
  void preloadAssets();

  /**
   * Get a cached asset (returns a copy to avoid modification issues)
   *
   * @param name Name of the cached asset
   * @return Copy of the cached drawable or nullptr if not found
   */
  std::unique_ptr<juce::Drawable> getCachedAsset(const juce::String &name);

  /**
   * Check if an asset is already cached
   */
  bool isAssetCached(const juce::String &name) const;

  /**
   * Clear all cached assets (useful for testing or theme switching)
   */
  void clearCache();

private:
  AssetLoader() = default;
  ~AssetLoader() = default;

  /**
   * Load SVG from file system
   */
  std::unique_ptr<juce::Drawable> loadSVGFromFile(const juce::File &file);

  /**
   * Get the assets directory path
   */
  juce::File getAssetsDirectory() const;

  // Cache of loaded drawables protected by cacheLock
  mutable juce::CriticalSection cacheLock;
  std::unordered_map<juce::String, std::unique_ptr<juce::Drawable>> assetCache;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AssetLoader)
};
