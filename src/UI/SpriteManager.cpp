#define STB_IMAGE_IMPLEMENTATION
#include <Libs/stb_image.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "UI/SpriteManager.h"
#include "Names/TypeNames.h"
#include "Pokemon/FormSpriteMapping.h"
#include "Utils/Logger.h"

using namespace Utils;

namespace UI
{
    namespace
    {
        // Decoded pixel data the Pokemon sprite cache may hold. One 256px HD sprite is 256 KB, so
        // this is roughly 190 of them -- several times the most that can be on screen at once (two
        // 30-slot boxes, the party and a held pokemon), which is what matters: a budget under the
        // working set would reload from ROMFS every frame instead of caching anything.
        constexpr size_t SPRITE_CACHE_BUDGET = 48u * 1024u * 1024u;

        size_t bytesOf(const Sprite *sourceBytes)
        {
            if (!sourceBytes || !sourceBytes->data)
                return 0;
            return static_cast<size_t>(sourceBytes->width) * static_cast<size_t>(sourceBytes->height) *
                   static_cast<size_t>(sourceBytes->channels);
        }
    }

    // Static member initialization
    std::map<uint32_t, SpriteManager::SpriteEntry> SpriteManager::spriteCache;
    std::list<uint32_t> SpriteManager::spriteLru;
    size_t SpriteManager::spriteBytes = 0;
    SpriteManager::EvictFn SpriteManager::evictCallback = nullptr;
    std::map<uint8_t, Sprite *> SpriteManager::typeSpriteCache;
std::map<uint32_t, Sprite *> SpriteManager::originMarkCache;
    std::map<std::string, Sprite *> SpriteManager::ribbonSpriteCache;
    bool SpriteManager::initialized = false;

    void SpriteManager::setEvictCallback(EvictFn callback) { evictCallback = callback; }

    size_t SpriteManager::cachedBytes() { return spriteBytes; }

    // Frees a sprite's pixel buffer, telling the renderer first so the texture keyed on that
    // buffer's address goes with it. Skipping that would leave a stale texture behind that a
    // later sprite landing on the same address would silently be drawn with.
    void SpriteManager::releaseSprite(Sprite *sprite)
    {
        if (!sprite)
            return;
        const size_t bytes = bytesOf(sprite);
        if (bytes)
        {
            spriteBytes = (spriteBytes >= bytes) ? spriteBytes - bytes : 0;
            if (evictCallback)
                evictCallback(sprite->data);
        }
        delete sprite;
    }

    void SpriteManager::trimToBudget()
    {
        // Never evict down to nothing: the entry just inserted is at the front, and the caller is
        // about to return a pointer to it.
        while (spriteBytes > SPRITE_CACHE_BUDGET && spriteLru.size() > 1)
        {
            const uint32_t key = spriteLru.back();
            spriteLru.pop_back();
            auto iterator = spriteCache.find(key);
            if (iterator != spriteCache.end())
            {
                releaseSprite(iterator->second.sprite);
                spriteCache.erase(iterator);
            }
        }
    }

    Sprite::~Sprite()
    {
        if (data)
        {
            stbi_image_free(data);
            data = nullptr;
        }
    }

    void SpriteManager::init()
    {
        if (initialized)
            return;

        logInfoToFile("SpriteManager initialized");
        initialized = true;
    }

    void SpriteManager::cleanup()
    {
        // The renderer is torn down before this runs and takes every texture with it, so drop the
        // callback rather than call into an object that is already gone.
        evictCallback = nullptr;

        for (auto &pair : spriteCache)
            delete pair.second.sprite;
        spriteCache.clear();
        spriteLru.clear();
        spriteBytes = 0;

        for (auto &pair : typeSpriteCache)
            delete pair.second;
        typeSpriteCache.clear();

        for (auto &pair : ribbonSpriteCache)
            delete pair.second;
        ribbonSpriteCache.clear();

        initialized = false;
        logInfoToFile("SpriteManager cleanup complete");
    }

    Sprite *SpriteManager::loadSprite(const std::string &path)
    {
        // Try to load from ROMFS
        std::string fullPath = "romfs:/" + path;

        int width, height, channels;
        unsigned char *data = stbi_load(fullPath.c_str(), &width, &height, &channels, 0);

        if (!data)
        {
            // Not an error — callers probe several paths (HD -> 96px -> base form).
            return nullptr;
        }

        Sprite *sprite = new Sprite();
        sprite->data = data;
        sprite->width = width;
        sprite->height = height;
        sprite->channels = channels;

        return sprite;
    }

    Sprite *SpriteManager::getSprite(uint16_t speciesId, bool isShiny)
    {
        // Delegate to form-aware version with base form (0)
        return getSprite(speciesId, 0, isShiny);
    }

    Sprite *SpriteManager::getSprite(uint16_t speciesId, uint8_t formId, bool isShiny)
    {
        if (!initialized)
            init();

        uint32_t cacheKey = makeCacheKey(speciesId, formId, isShiny, false);

        // Check cache first; a hit is also the "recently used" signal that keeps it from being evicted.
        auto iterator = spriteCache.find(cacheKey);
        if (iterator != spriteCache.end())
        {
            spriteLru.splice(spriteLru.begin(), spriteLru, iterator->second.lru);
            return iterator->second.sprite;
        }

        // Resolve the sprite stem for this form. Prefer the NAME-keyed table: the peer-form
        // families (Unown letters, Arceus/Silvally types, Vivillon patterns, Alcremie creams,
        // Furfrou trims, flower colours, seasons, seas) have no numeric PokeAPI id at all, so
        // getFormSpriteId() cannot reach them. It returns "" for everything else.
        std::string suffix = isShiny ? "s" : "";
        const char *named = Pokemon::getFormSpriteName(speciesId, formId);
        std::string spriteStem = (named && named[0] != '\0') ? std::string(named) : std::to_string(Pokemon::getFormSpriteId(speciesId, formId));

        // Prefer the HD render (transparent HOME PNG) for this sprite id, then
        // fall back to the bundled 96px sprite.
        Sprite *sprite = loadSprite("sprites/pokemon_hd/" + spriteStem + suffix + ".png");
        if (!sprite)
        {
            sprite = loadSprite("sprites/pokemon/" + spriteStem + suffix + ".png");
        }

        // If a form had no sprite of its own, fall back to the base species (HD then 96px).
        if (!sprite && formId > 0)
        {
            std::string base = std::to_string(speciesId);
            sprite = loadSprite("sprites/pokemon_hd/" + base + suffix + ".png");
            if (!sprite)
            {
                sprite = loadSprite("sprites/pokemon/" + base + suffix + ".png");
            }
        }

        // Cache it (even if nullptr, so we don't keep trying to load missing sprites)
        spriteLru.push_front(cacheKey);
        spriteCache[cacheKey] = SpriteEntry{sprite, spriteLru.begin()};
        spriteBytes += bytesOf(sprite);
        trimToBudget(); // stops at one entry, so the sprite being returned is never the one freed

        return sprite;
    }

    Sprite *SpriteManager::getIconSprite(uint16_t speciesId, bool isShiny)
    {
        // Delegate to form-aware version with base form (0)
        return getIconSprite(speciesId, 0, isShiny);
    }

    Sprite *SpriteManager::getIconSprite(uint16_t speciesId, uint8_t formId, bool isShiny)
    {
        // We don't have separate icon sprites, so just use regular sprites
        // The sprite will be cached by getSprite() to avoid duplicate allocations
        return getSprite(speciesId, formId, isShiny);
    }

    bool SpriteManager::spriteExists(uint16_t speciesId, bool isShiny)
    {
        Sprite *sprite = getSprite(speciesId, isShiny);
        return sprite != nullptr;
    }

    Sprite *SpriteManager::getOriginMarkSprite(Trainer::OriginMark mark, Color markColor)
    {
        if (!initialized)
            init();

        const char *stem = Trainer::originMarkFileStem(mark);
        if (stem == nullptr)
            return nullptr; // Gen 3/4/5 and anything the games never marked

        const uint32_t key = static_cast<uint32_t>(mark) | (static_cast<uint32_t>(markColor.red) << 8) | (static_cast<uint32_t>(markColor.green) << 16) | (static_cast<uint32_t>(markColor.blue) << 24);
        auto iterator = originMarkCache.find(key);
        if (iterator != originMarkCache.end())
            return iterator->second;

        // loadSprite hands over OWNERSHIP of a fresh decode -- it caches nothing -- so the source
        // is released as soon as the tinted copy is built.
        Sprite *source = loadSprite(std::string("sprites/marks/") + stem + ".png");
        Sprite *tinted = nullptr;
        if (source != nullptr && source->data != nullptr && source->channels == 4)
        {
            // RECOLOUR, KEEPING ALPHA. The mark is a black silhouette whose shape lives entirely in
            // the alpha channel, so replacing RGB outright is the whole operation -- no blending,
            // and the antialiased edge survives because its alpha is untouched.
            const size_t pixelCount = static_cast<size_t>(source->width) * source->height;
            // std::malloc, NOT new[]: ~Sprite() frees with stbi_image_free (which is free()), so a
            // new[] buffer here would be a mismatched deallocation the moment the sprite dies.
            unsigned char *pixels = static_cast<unsigned char *>(std::malloc(pixelCount * 4));
            if (pixels != nullptr)
            {
                for (size_t pixel = 0; pixel < pixelCount; ++pixel)
                {
                    pixels[pixel * 4 + 0] = markColor.red;
                    pixels[pixel * 4 + 1] = markColor.green;
                    pixels[pixel * 4 + 2] = markColor.blue;
                    pixels[pixel * 4 + 3] = source->data[pixel * 4 + 3];
                }
                tinted = new Sprite();
                tinted->width = source->width;
                tinted->height = source->height;
                tinted->channels = 4;
                tinted->data = pixels;
            }
        }
        delete source;
        originMarkCache[key] = tinted; // a failed load is cached too, so romfs is asked once
        return tinted;
    }

    Sprite *SpriteManager::getRibbonSprite(const std::string &spriteStem)
    {
        if (!initialized)
            init();

        auto iterator = ribbonSpriteCache.find(spriteStem);
        if (iterator != ribbonSpriteCache.end())
            return iterator->second;

        Sprite *sprite = loadSprite("sprites/ribbons/" + spriteStem + ".png");
        ribbonSpriteCache[spriteStem] = sprite; // a failed load is cached too, so romfs is asked once
        return sprite;
    }

    Sprite *SpriteManager::getTypeSprite(uint8_t typeId)
    {
        if (!initialized)
            init();

        // Bounded by the NAME count (19), not the species type count (18): index 18 is Stellar,
        // which only a Tera type can be, and it has an icon like every other type.
        if (typeId >= Names::getTypeNameCount())
        {
            return nullptr;
        }

        auto iterator = typeSpriteCache.find(typeId);
        if (iterator != typeSpriteCache.end())
        {
            return iterator->second;
        }

        // Build type sprite path
        // Type sprites are stored as 0.png, 1.png, etc. in sprites/types/
        std::string path = "sprites/types/";
        path += std::to_string(typeId);
        path += ".png";

        Sprite *sprite = loadSprite(path);

        // Cache it (even if nullptr)
        typeSpriteCache[typeId] = sprite;

        return sprite;
    }

    bool SpriteManager::typeSpriteExists(uint8_t typeId)
    {
        Sprite *sprite = getTypeSprite(typeId);
        return sprite != nullptr;
    }
}