#pragma once
#include <filesystem>

#include "afp.h"

// ReSharper disable CppInconsistentNaming
using FILE_TYPE = DWORD;
using FILE_CREATOR = DWORD;
// ReSharper restore CppInconsistentNaming

enum desktop_params
{
    do_not_use_index = 0,
    max_open_desktops = 32,
    max_comment_size = 199,
    max_desktop_data_size = 1024
};

#define MAX_APPL_PATHNAME		MAX_AFP_STRING_LEN
#define DESKTOP_FILE_NAME		TEXT("afp_desktop.db")

enum desk_item_types : UINT8
{
    entry_type_empty = 0,
    entry_type_icon = 1,
    entry_type_appl = 2,
    entry_type_comment = 3,
    entry_type_reserved = 4
};

struct open_desktop
{
    UINT16 volume_id;
    UINT16 desktop_ref_num;
};

struct afp_desk_item
{
    afp_desk_item() : item_type(entry_type_empty)
                      , file_type(0)
                      , file_creator(0)
                      , tag(0)
                      , dir_id(0)
                      , icon_type(0)
                      , data_size(0)
                      , path_type(0)
                      , path_name{}
                      , data{}
    {
    }

    afp_desk_item(
        const desk_item_types item_type,
        const FILE_TYPE file_type,
        const FILE_CREATOR file_creator,
        const UINT32 tag,
        const UINT32 dir_id,
        const UINT8 icon_type,
        const UINT16 data_size,
        const INT8 path_type,
        const WCHAR* path_name,
        const char* data
    ) : item_type(item_type)
        , file_type(file_type)
        , file_creator(file_creator)
        , tag(tag)
        , dir_id(dir_id)
        , icon_type(icon_type)
        , data_size(data_size)
        , path_type(path_type)
        , data{}
    {
        wcscpy_s(this->path_name, sizeof(this->path_name), path_name);
        memcpy_s(this->data, sizeof(this->data), data, data_size);
    }

    desk_item_types item_type;

    FILE_TYPE file_type;
    FILE_CREATOR file_creator;

    UINT32 tag;
    UINT32 dir_id;
    UINT8 icon_type;
    UINT16 data_size;
    INT8 path_type;

    WCHAR path_name[MAX_AFP_NAME];
    char data[max_desktop_data_size];

    bool operator==(const afp_desk_item* item) const
    {
        return
            std::tie(item_type, file_type, file_creator, tag, dir_id, icon_type, data_size, path_type) ==
            std::tie(item->item_type, item->file_type, item->file_creator, item->tag, item->dir_id, item->icon_type,
                     item->data_size, item->path_type) &&
            wcscmp(path_name, item->path_name) == 0 &&
            memcmp(data, item->data, data_size) == 0;
    }
};
