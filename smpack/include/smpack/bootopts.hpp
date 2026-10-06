// 'exec/boot-options.json' editing.

#pragma once

#include <algorithm>
#include <string>

#include "smpack/json.hpp"

namespace smpack::bootopts
{
    inline constexpr std::size_t MAX_PATCHES = 32;
    inline constexpr std::size_t MAX_NAME_LEN = 31;

    [[nodiscard]] inline Result<json::Value *> Entry(
        json::Value & doc
    )
    {
        auto & arr = doc["allcontentidarray"];

        if (!arr.IsArray() || arr.AsArray().empty())
        {
            return Fail( Errc::INCONSISTENT, "boot-options.json has no 'allcontentidarray' entry" );
        }

        return &arr.AsArray()[0];
    }


    // Add [or remove] a pack name. Returns 'true' when the document changed.
    [[nodiscard]] inline Result<bool> SetPatch(
        json::Value & doc,
        std::string_view key,
        std::string_view name,
        bool add
    )
    {
        if (name.size() > MAX_NAME_LEN)
        {
            return Fail( Errc::BAD_ARGUMENT, std::format( "patch pack name '{}' exceeds {} characters", name, MAX_NAME_LEN ) );
        }

        SMPACK_TRY( e, Entry( doc ) );
        auto & list = (*e)[key];

        if (!list.IsArray())
            list = json::Value::MakeArray();

        auto & a = list.AsArray();

        auto it = std::find_if( a.begin(), a.end(), [&] ( const json::Value & v )
        {
            return v.AsString() == name;
        } );

        if (add)
        {
            if (it != a.end())
                return false;

            if (a.size() >= MAX_PATCHES)
                return Fail( Errc::UNSUPPORTED, std::format( "{} already has {} entries", key, MAX_PATCHES ) );

            // Make the most recently installed mod win. Texture packs are searched
            // first match [insert at front], lod packs last registration wins.
            if (key == "patch-lodpacks")
                a.push_back( json::Value( std::string( name ) ) );
            else
                a.insert( a.begin(), json::Value( std::string( name ) ) );

            return true;
        }

        if (it == a.end())
            return false;

        a.erase( it );
        return true;
    }
}