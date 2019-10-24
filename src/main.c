#include "cache.h"
#include "fuse_local.h"
#include "network.h"

#include <getopt.h>
#include <stdlib.h>
#include <string.h>

void add_arg(char ***fuse_argv_ptr, int *fuse_argc, char *opt_string);
static void print_help(char *program_name, int long_help);
static void print_version();
static void print_long_help();
static int
parse_arg_list(int argc, char **argv, char ***fuse_argv, int *fuse_argc);
void parse_config_file(char ***argv, int *argc);

int main(int argc, char **argv)
{
    /* Automatically print help if not enough arguments are supplied */
    if (argc < 2) {
        print_help(argv[0], 0);
        fprintf(stderr, "For more information, run \"%s --help.\"\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    /* These are passed into fuse initialiser */
    char **fuse_argv = NULL;
    int fuse_argc = 0;
    /* These are the combined argument with the config file */
    char **all_argv = NULL;
    int all_argc = 0;

    /*--- Add the program's name to the combined argument list ---*/
    add_arg(&all_argv, &all_argc, argv[0]);
    /*--- FUSE expects the first initialisation to be the program's name ---*/
    add_arg(&fuse_argv, &fuse_argc, argv[0]);

    /* initialise network configuration struct */
    Config_init();

    /* initialise network subsystem */
    NetworkSystem_init();

    /* parse the config file, if it exists, store it in all_argv and all_argc */
    parse_config_file(&all_argv, &all_argc);

    /* Copy the command line argument list to the combined argument list */
    for (int i = 1; i < argc; i++) {
        add_arg(&all_argv, &all_argc, argv[i]);
    }

    /* parse the combined argument list */
    if (parse_arg_list(all_argc, all_argv, &fuse_argv, &fuse_argc)) {
        /*
         * The user basically didn't supply enough arguments, if we reach here
         * The point is to print some error messages
         */
        goto fuse_start;
    }

    /*--- Add the last remaining argument, which is the mountpoint ---*/
    add_arg(&fuse_argv, &fuse_argc, argv[argc-1]);

    /* The second last remaining argument is the URL */
    char *base_url = argv[argc-2];
    if (strncmp(base_url, "http://", 7) && strncmp(base_url, "https://", 8)) {
        fprintf(stderr, "Error: Please supply a valid URL.\n");
        print_help(argv[0], 0);
        exit(EXIT_FAILURE);
    } else {
        if (CONFIG.sonic_username && CONFIG.sonic_password) {
            CONFIG.sonic_mode = 1;
        } else if (CONFIG.sonic_username || CONFIG.sonic_password) {
            fprintf(stderr,
                    "Error: You have to supply both username and password to\
activate Sonic mode.\n");
            exit(EXIT_FAILURE);
        }
        if(!LinkSystem_init(base_url)) {
            fprintf(stderr, "Error: Network initialisation failed.\n");
            exit(EXIT_FAILURE);
        }
    }

    fuse_start:
    fuse_local_init(fuse_argc, fuse_argv);

    return 0;
}

void parse_config_file(char ***argv, int *argc)
{
    char *xdg_config_home = getenv("XDG_CONFIG_HOME");
    if (!xdg_config_home) {
        char *home = getenv("HOME");
        char *xdg_config_home_default = "/.config";
        xdg_config_home = path_append(home, xdg_config_home_default);
    }
    char *full_path = path_append(xdg_config_home, "/httpdirfs/config");

    /* The buffer has to be able to fit a URL */
    int buf_len = MAX_PATH_LEN;
    char buf[buf_len];
    FILE *config = fopen(full_path, "r");
    if (config) {
        while (fgets(buf, buf_len, config)) {
            if (buf[0] == '-') {
                (*argc)++;
                buf[strnlen(buf, buf_len) - 1] = '\0';
                char *space;
                space = strchr(buf, ' ');
                if (!space) {
                    *argv = realloc(*argv, *argc * sizeof(char **));
                    (*argv)[*argc - 1] = strndup(buf, buf_len);
                } else {
                    (*argc)++;
                    *argv = realloc(*argv, *argc * sizeof(char **));
                    /* Only copy up to the space character*/
                    (*argv)[*argc - 2] = strndup(buf, space - buf);
                    /* Starts copying after the space */
                    (*argv)[*argc - 1] = strndup(space + 1,
                                                buf_len - (space + 1 - buf));
                }
            }
        }
    }
}

static int
parse_arg_list(int argc, char **argv, char ***fuse_argv, int *fuse_argc)
{
    char c;
    int long_index = 0;
    const char *short_opts = "o:hVdfsp:u:P:";
    const struct option long_opts[] = {
        /* Note that 'L' is returned for long options */
        {"help", no_argument, NULL, 'h'},                   /* 0  */
        {"version", no_argument, NULL, 'V'},                /* 1  */
        {"debug", no_argument, NULL, 'd'},                  /* 2  */
        {"username", required_argument, NULL, 'u'},         /* 3  */
        {"password", required_argument, NULL, 'p'},         /* 4  */
        {"proxy", required_argument, NULL, 'P'},            /* 5  */
        {"proxy-username", required_argument, NULL, 'L'},   /* 6  */
        {"proxy-password", required_argument, NULL, 'L'},   /* 7  */
        {"cache", no_argument, NULL, 'L'},                  /* 8  */
        {"dl-seg-size", required_argument, NULL, 'L'},      /* 9  */
        {"max-seg-count", required_argument, NULL, 'L'},    /* 10 */
        {"max-conns", required_argument, NULL, 'L'},        /* 11 */
        {"user-agent", required_argument, NULL, 'L'},       /* 12 */
        {"retry-wait", required_argument, NULL, 'L'},       /* 13 */
        {"cache-location", required_argument, NULL, 'L'},   /* 14 */
        {"sonic-username", required_argument, NULL, 'L'},   /* 15 */
        {"sonic-password", required_argument, NULL, 'L'},   /* 16 */
        {0, 0, 0, 0}
    };
    while ((c =
        getopt_long(argc, argv, short_opts, long_opts,
                    &long_index)) != -1) {
        switch (c) {
            case 'o':
                add_arg(fuse_argv, fuse_argc, "-o");
                add_arg(fuse_argv, fuse_argc, optarg);
                break;
            case 'h':
                print_help(argv[0], 1);
                add_arg(fuse_argv, fuse_argc, "-ho");
                /* skip everything else to print the help */
                return 1;
            case 'V':
                print_version(argv[0], 1);
                add_arg(fuse_argv, fuse_argc, "-V");
                return 1;
            case 'd':
                add_arg(fuse_argv, fuse_argc, "-d");
                break;
            case 'f':
                add_arg(fuse_argv, fuse_argc, "-f");
                break;
            case 's':
                add_arg(fuse_argv, fuse_argc, "-s");
                break;
            case 'u':
                CONFIG.http_username = strdup(optarg);
                break;
            case 'p':
                CONFIG.http_password = strdup(optarg);
                break;
            case 'P':
                CONFIG.proxy = strdup(optarg);
                break;
            case 'L':
                /* Long options */
                switch (long_index) {
                    case 6:
                        CONFIG.proxy_username = strdup(optarg);
                        break;
                    case 7:
                        CONFIG.proxy_password = strdup(optarg);
                        break;
                    case 8:
                        CONFIG.cache_enabled = 1;
                        break;
                    case 9:
                        CONFIG.data_blksz = atoi(optarg) * 1024 * 1024;
                        break;
                    case 10:
                        CONFIG.max_segbc = atoi(optarg);
                        break;
                    case 11:
                        CONFIG.max_conns = atoi(optarg);
                        break;
                    case 12:
                        CONFIG.user_agent = strdup(optarg);
                        break;
                    case 13:
                        CONFIG.http_wait_sec = atoi(optarg);
                        break;
                    case 14:
                        CONFIG.cache_dir = strdup(optarg);
                        break;
                    case 15:
                        CONFIG.sonic_username = strdup(optarg);
                        break;
                    case 16:
                        CONFIG.sonic_password = strdup(optarg);
                        break;
                    default:
                        fprintf(stderr, "see httpdirfs -h for usage\n");
                        return 1;
                }
                break;
            default:
                fprintf(stderr, "see httpdirfs -h for usage\n");
                return 1;
        }
    };
    return 0;
}

/**
 * \brief add an argument to an argv array
 * \details This is basically how you add a string to an array of string
 */
void add_arg(char ***fuse_argv_ptr, int *fuse_argc, char *opt_string)
{
    (*fuse_argc)++;
    *fuse_argv_ptr = realloc(*fuse_argv_ptr, *fuse_argc * sizeof(char *));
    char **fuse_argv = *fuse_argv_ptr;
    fuse_argv[*fuse_argc - 1] = strdup(opt_string);
}

static void print_help(char *program_name, int long_help)
{
    fprintf(stderr,
            "usage: %s [options] URL mountpoint\n", program_name);
    if (long_help) {
        print_long_help();
    }
}

static void print_version()
{
    fprintf(stderr,
            "HTTPDirFS version " VERSION "\n");
}

static void print_long_help()
{
    fprintf(stderr,
"\n\
general options:\n\
    -o opt,[opt...]        mount options\n\
    -h   --help            print help\n\
    -V   --version         print version\n\
\n\
HTTPDirFS options:\n\
    -u  --username          HTTP authentication username\n\
    -p  --password          HTTP authentication password\n\
    -P  --proxy             Proxy for libcurl, for more details refer to\n\
                            https://curl.haxx.se/libcurl/c/CURLOPT_PROXY.html\n\
        --proxy-username    Username for the proxy\n\
        --proxy-password    Password for the proxy\n\
        --cache             Enable cache (default: off)\n\
        --cache-location    Set a custom cache location\n\
                            (default: \"${XDG_CACHE_HOME}/httpdirfs\")\n\
        --dl-seg-size       Set cache download segment size, in MB (default: 8)\n\
                            Note: this setting is ignored if previously\n\
                            cached data is found for the requested file.\n\
        --max-seg-count     Set maximum number of download segments a file\n\
                            can have. (default: 128*1024)\n\
                            With the default setting, the maximum memory usage\n\
                            per file is 128KB. This allows caching files up\n\
                            to 1TB in size using the default segment size.\n\
        --max-conns         Set maximum number of network connections that\n\
                            libcurl is allowed to make. (default: 10)\n\
        --retry-wait        Set delay in seconds before retrying an HTTP request\n\
                            after encountering an error. (default: 5)\n\
        --user-agent        Set user agent string (default: \"HTTPDirFS\")\n\

    For mounting a Airsonic / Subsonic server:\n\
        --sonic-username    The username for your Airsonic / Subsonic server\n\
        --sonic-password    The username for your Airsonic / Subsonic server\n\
\n\");
}
