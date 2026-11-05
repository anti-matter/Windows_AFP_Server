#pragma once

#include "afp.h"
#include "afphelp.h"

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

    desk_item_types item_type;

    FILE_TYPE file_type;
    FILE_CREATOR file_creator;

    UINT32 tag;
    INT32 dir_id;
    UINT8 icon_type;
    UINT16 data_size;
    UINT8 path_type;

    WCHAR path_name[MAX_AFP_NAME];
    char data[max_desktop_data_size];

    bool equal_comment(const afp_desk_item* item) const
    {
        return item_type == entry_type_comment && item->item_type == entry_type_comment &&
            dir_id == item->dir_id &&
            lstrcmp(path_name, item->path_name) == 0;
    }

    bool equal_aapl(const afp_desk_item* item) const
    {
        return item_type == entry_type_appl && item->item_type == entry_type_appl &&
            file_creator == item->file_creator;
    }

    bool equal_icon(const afp_desk_item* item) const
    {
        return item_type == entry_type_icon && item->item_type == entry_type_icon &&
            file_creator == item->file_creator &&
            file_type == item->file_type &&
            icon_type == item->icon_type;
    }

    static std::wstring convert_to_os_type(const DWORD value)
    {
        union union_char
        {
            DWORD d_word;
            char c_array[4];
        };

        std::string result;
        union union_char temp;

        temp.d_word = value;

        result.push_back(temp.c_array[3]);
        result.push_back(temp.c_array[2]);
        result.push_back(temp.c_array[1]);
        result.push_back(temp.c_array[0]);

        return string_to_wide_string(result, CP_ACP);
    }

    [[nodiscard]] std::wstring creator_as_string() const
    {
        return convert_to_os_type(file_creator);
    }

    [[nodiscard]] std::wstring file_type_as_string() const
    {
        return convert_to_os_type(file_type);
    }

#pragma warning (push)
#pragma warning(disable: 6386)
    afp_desk_item* operator=(const afp_desk_item* item)  // NOLINT(misc-unconventional-assign-operator)
    {
        this->item_type = item->item_type;
        this->file_type = item->file_type;
        this->file_creator = item->file_creator;
        this->tag = item->tag;
        this->dir_id = item->dir_id;
        this->icon_type = item->icon_type;
        this->data_size = item->data_size;
        this->path_type = item->path_type;
        wcscpy_s(this->path_name, sizeof(this->path_name), item->path_name);
        memcpy_s(this->data, sizeof(data), item->data, sizeof(data));

        return this;
    }
#pragma warning (pop)
};
