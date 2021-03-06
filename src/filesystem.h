#ifndef __FILESYSTEM_H__
#define __FILESYSTEM_H__

#include <cstdint>
#include <cstring>
#include <functional>
#include "utils/iostream.h"

namespace ZZT {
    struct FileEntry {
        const char *filename; // Don't copy the pointer!
        bool is_dir;
    };

    class FilesystemDriver {
    protected:
        bool read_only;

        FilesystemDriver(bool read_only);

    public:
        virtual ~FilesystemDriver() { }

        // required
        virtual IOStream *open_file(const char *filename, bool write) = 0;
        virtual bool list_files(std::function<bool(FileEntry&)> callback) = 0;

        virtual bool is_path_driver(void);

        inline bool is_read_only() {
            return read_only;
        }
    };

    class NullFilesystemDriver: public FilesystemDriver {
    public:
        NullFilesystemDriver();

        virtual IOStream *open_file(const char *filename, bool write) override;
        virtual bool list_files(std::function<bool(FileEntry&)> callback) override;
    };

    class PathFilesystemDriver: public FilesystemDriver {
    protected:
        size_t max_path_length;
        char separator;
        char *current_path;

        PathFilesystemDriver(const char *starting_path, size_t max_path_length, char separator, bool read_only);
        ~PathFilesystemDriver();

    public:
        IOStream *open_file(const char *filename, bool write) override;
        bool is_path_driver(void) override;

        virtual bool open_dir(const char *name);
        virtual bool join_path(char *dest, size_t len, const char *curr, const char *next);
        virtual bool split_path(const char *path, char *parent, size_t parent_len, char *filename, size_t filename_len);

        inline const char *get_current_path() const { return current_path; }
        inline void set_current_path(const char *path) {
            strncpy(current_path, path, max_path_length);
            current_path[max_path_length] = 0;
        }
        inline size_t get_max_path_length() const {
            return max_path_length;
        }

        // required
        virtual IOStream *open_file_absolute(const char *filename, bool write) = 0;
        virtual bool has_parent(void) = 0;
   };
}

#endif
