/* File: cmd4.c */

/*
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 *
 * This software may be copied and distributed for educational, research,
 * and not for profit purposes provided that this copyright and statement
 * are included in all such copies. Other copyrights may also apply.
 */

/* Purpose: Interface commands */

#include "angband.h"
#include "equip.h"
#include "int-map.h"
#include "z-doc.h"

#include <assert.h>
#include <time.h>

static void browser_cursor(char ch, int *column, int *grp_cur, int grp_cnt, int *list_cur, int list_cnt);

/*
 * A set of functions to maintain automatic dumps of various kinds.
 * -Mogami-
 *
 * remove_auto_dump(orig_file, mark)
 *     Remove the old automatic dump of type "mark".
 * auto_dump_printf(fmt, ...)
 *     Dump a formatted string using fprintf().
 * open_auto_dump(buf, mark)
 *     Open a file, remove old dump, and add new header.
 * close_auto_dump(void)
 *     Add a footer, and close the file.
 *
 *    The dump commands of original Angband simply add new lines to
 * existing files; these files will become bigger and bigger unless
 * an user deletes some or all of these files by hand at some
 * point.
 *
 *     These three functions automatically delete old dumped lines
 * before adding new ones. Since there are various kinds of automatic
 * dumps in a single file, we add a header and a footer with a type
 * name for every automatic dump, and kill old lines only when the
 * lines have the correct type of header and footer.
 *
 *     We need to be quite paranoid about correctness; the user might
 * (mistakenly) edit the file by hand, and see all their work come
 * to nothing on the next auto dump otherwise. The current code only
 * detects changes by noting inconsistencies between the actual number
 * of lines and the number written in the footer. Note that this will
 * not catch single-line edits.
 */

/*
 *  Mark strings for auto dump
 */
static char auto_dump_header[] = "# vvvvvvv== %s ==vvvvvvv";
static char auto_dump_footer[] = "# ^^^^^^^== %s ==^^^^^^^";

/*
 * Variables for auto dump
 */
static FILE *auto_dump_stream;
static cptr auto_dump_mark;
static int auto_dump_line_num;

#define CONFIG_PREF_FILE "user-config.prf"
#define CONFIG_MAX_SLOTS 17
#define CONFIG_DESC_LEN 80
#define CONFIG_CURRENT_SLOT 0
#define CONFIG_FIRST_USER_SLOT 1
#define CONFIG_LAST_USER_SLOT 16
#define CONFIG_FIRST_ALL_SLOT 11

typedef struct config_slot_info_s config_slot_info_t;

struct config_slot_info_s
{
    bool used;
    char desc[CONFIG_DESC_LEN];
};

/*
 * Remove old lines automatically generated before.
 */
static void remove_auto_dump(cptr orig_file)
{
    FILE *tmp_fff, *orig_fff;

    char tmp_file[1024];
    char buf[1024];
    bool between_mark = FALSE;
    bool changed = FALSE;
    int line_num = 0;
    long header_location = 0;
    char header_mark_str[80];
    char footer_mark_str[80];
    size_t mark_len;

    /* Prepare a header/footer mark string */
    sprintf(header_mark_str, auto_dump_header, auto_dump_mark);
    sprintf(footer_mark_str, auto_dump_footer, auto_dump_mark);

    mark_len = strlen(footer_mark_str);

    /* Open an old dump file in read-only mode */
    orig_fff = my_fopen(orig_file, "r");

    /* If original file does not exist, nothing to do */
    if (!orig_fff) return;

    /* Open a new (temporary) file */
    tmp_fff = my_fopen_temp(tmp_file, 1024);

    if (!tmp_fff)
    {
        msg_format("Failed to create temporary file %s.", tmp_file);
        msg_print(NULL);
        return;
    }

    /* Loop for every line */
    while (TRUE)
    {
        /* Read a line */
        if (my_fgets(orig_fff, buf, sizeof(buf)))
        {
            /* Read error: Assume End of File */

            /*
             * Was looking for the footer, but not found.
             *
             * Since automatic dump might be edited by hand,
             * it's dangerous to kill these lines.
             * Seek back to the next line of the (pseudo) header,
             * and read again.
             */
            if (between_mark)
            {
                fseek(orig_fff, header_location, SEEK_SET);
                between_mark = FALSE;
                continue;
            }

            /* Success -- End the loop */
            else
            {
                break;
            }
        }

        /* We are looking for the header mark of automatic dump */
        if (!between_mark)
        {
            /* Is this line a header? */
            if (!strcmp(buf, header_mark_str))
            {
                /* Memorise seek point of this line */
                header_location = ftell(orig_fff);

                /* Initialize counter for number of lines */
                line_num = 0;

                /* Look for the footer from now */
                between_mark = TRUE;

                /* There are some changes */
                changed = TRUE;
            }

            /* Not a header */
            else
            {
                /* Copy orginally lines */
                fprintf(tmp_fff, "%s\n", buf);
            }
        }

        /* We are looking for the footer mark of automatic dump */
        else
        {
            /* Is this line a footer? */
            if (!strncmp(buf, footer_mark_str, mark_len))
            {
                int tmp;

                /*
                 * Compare the number of lines
                 *
                 * If there is an inconsistency between
                 * actual number of lines and the
                 * number here, the automatic dump
                 * might be edited by hand. So it's
                 * dangerous to kill these lines.
                 * Seek back to the next line of the
                 * (pseudo) header, and read again.
                 */
                if (!sscanf(buf + mark_len, " (%d)", &tmp)
                    || tmp != line_num)
                {
                    fseek(orig_fff, header_location, SEEK_SET);
                }

                /* Look for another header */
                between_mark = FALSE;
            }

            /* Not a footer */
            else
            {
                /* Ignore old line, and count number of lines */
                line_num++;
            }
        }
    }

    /* Close files */
    my_fclose(orig_fff);
    my_fclose(tmp_fff);

    /* If there are some changes, overwrite the original file with new one */
    if (changed)
    {
        /* Copy contents of temporary file */

        tmp_fff = my_fopen(tmp_file, "r");
        orig_fff = my_fopen(orig_file, "w");

        while (!my_fgets(tmp_fff, buf, sizeof(buf)))
            fprintf(orig_fff, "%s\n", buf);

        my_fclose(orig_fff);
        my_fclose(tmp_fff);
    }

    /* Kill the temporary file */
    fd_kill(tmp_file);

    return;
}


/*
 * Dump a formatted line, using "vstrnfmt()".
 */
static void auto_dump_printf(cptr fmt, ...)
{
    cptr p;
    va_list vp;

    char buf[1024];

    /* Begin the Varargs Stuff */
    va_start(vp, fmt);

    /* Format the args, save the length */
    (void)vstrnfmt(buf, sizeof(buf), fmt, vp);

    /* End the Varargs Stuff */
    va_end(vp);

    /* Count number of lines */
    for (p = buf; *p; p++)
    {
        if (*p == '\n') auto_dump_line_num++;
    }

    /* Dump it */
    fprintf(auto_dump_stream, "%s", buf);
}


/*
 *  Open file to append auto dump.
 */
static bool open_auto_dump(cptr buf, cptr mark)
{

    char header_mark_str[80];

    /* Save the mark string */
    auto_dump_mark = mark;

    /* Prepare a header mark string */
    sprintf(header_mark_str, auto_dump_header, auto_dump_mark);

    /* Remove old macro dumps */
    remove_auto_dump(buf);

    /* Append to the file */
    auto_dump_stream = my_fopen(buf, "a");

    /* Failure */
    if (!auto_dump_stream) {
        msg_format("Failed to open %s.", buf);
        msg_print(NULL);

        /* Failed */
        return FALSE;
    }

    /* Start dumping */
    fprintf(auto_dump_stream, "%s\n", header_mark_str);

    /* Initialize counter */
    auto_dump_line_num = 0;

    auto_dump_printf("# *Warning!*  The lines below are an automatic dump.\n");
    auto_dump_printf("# Don't edit them; changes will be deleted and replaced automatically.\n");

    /* Success */
    return TRUE;
}

/*
 *  Append foot part and close auto dump.
 */
static void close_auto_dump(void)
{
    char footer_mark_str[80];

    /* Prepare a footer mark string */
    sprintf(footer_mark_str, auto_dump_footer, auto_dump_mark);

    auto_dump_printf("# *Warning!*  The lines above are an automatic dump.\n");
    auto_dump_printf("# Don't edit them; changes will be deleted and replaced automatically.\n");

    /* End of dump */
    fprintf(auto_dump_stream, "%s (%d)\n", footer_mark_str, auto_dump_line_num);

    /* Close */
    my_fclose(auto_dump_stream);

    return;
}

static void config_build_path(char *buf, size_t buf_len)
{
    path_build(buf, buf_len, ANGBAND_DIR_USER, CONFIG_PREF_FILE);
}

static void config_build_mark(char *buf, size_t buf_len, cptr section, int slot)
{
    if (slot == CONFIG_CURRENT_SLOT)
        strnfmt(buf, buf_len, "config:%s:0", section);
    else
        strnfmt(buf, buf_len, "config:%s:%c", section, 'A' + (slot - CONFIG_FIRST_USER_SLOT));
}

static bool config_parse_header_line(cptr line, char *mark, size_t mark_len)
{
    const char *prefix = "# vvvvvvv== ";
    const char *suffix = " ==vvvvvvv";
    const char *start;
    const char *end;
    size_t len;

    if (strncmp(line, prefix, strlen(prefix)) != 0) return FALSE;

    start = line + strlen(prefix);
    end = strstr(start, suffix);
    if (!end) return FALSE;

    len = (size_t)(end - start);
    if (len >= mark_len) len = mark_len - 1;
    strnfmt(mark, mark_len, "%.*s", (int)len, start);
    return TRUE;
}

static bool config_parse_mark(cptr mark, char *section, size_t section_len, int *slot)
{
    const char *prefix = "config:";
    const char *start;
    const char *sep;

    if (strncmp(mark, prefix, strlen(prefix)) != 0) return FALSE;
    start = mark + strlen(prefix);
    sep = strrchr(start, ':');
    if (!sep || !sep[1]) return FALSE;

    if (sep[1] == '0')
    {
        *slot = CONFIG_CURRENT_SLOT;
        strnfmt(section, section_len, "%.*s", (int)(sep - start), start);
        return TRUE;
    }

    {
        int slot_char = toupper((unsigned char)sep[1]);
        int max_letter = 'A' + (CONFIG_LAST_USER_SLOT - CONFIG_FIRST_USER_SLOT);

        if (slot_char < 'A' || slot_char > max_letter)
            return FALSE;

        *slot = (slot_char - 'A') + CONFIG_FIRST_USER_SLOT;
    }
    strnfmt(section, section_len, "%.*s", (int)(sep - start), start);
    return TRUE;
}

static void config_timestamp(char *buf, size_t buf_len)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    if (!tm_info)
    {
        strnfmt(buf, buf_len, "Unknown time");
        return;
    }

    strftime(buf, buf_len, "%Y-%m-%d %H:%M", tm_info);
}

static void config_scan_slots(cptr section, config_slot_info_t slots[CONFIG_MAX_SLOTS])
{
    FILE *fp;
    char path[1024];
    char buf[1024];
    char mark[128];
    char mark_section[64];
    int mark_slot = -1;
    bool in_block = FALSE;
    bool desc_found = FALSE;
    char footer_mark_str[128];
    size_t footer_len;

    for (int i = 0; i < CONFIG_MAX_SLOTS; i++)
    {
        slots[i].used = FALSE;
        slots[i].desc[0] = '\0';
    }

    config_build_path(path, sizeof(path));
    fp = my_fopen(path, "r");
    if (!fp) return;

    while (TRUE)
    {
        if (my_fgets(fp, buf, sizeof(buf))) break;

        if (config_parse_header_line(buf, mark, sizeof(mark)))
        {
            if (config_parse_mark(mark, mark_section, sizeof(mark_section), &mark_slot)
                && streq(mark_section, section)
                && mark_slot >= 0
                && mark_slot < CONFIG_MAX_SLOTS)
            {
                slots[mark_slot].used = TRUE;
                if (!slots[mark_slot].desc[0])
                    strnfmt(slots[mark_slot].desc, sizeof(slots[mark_slot].desc), "Unnamed");

                sprintf(footer_mark_str, auto_dump_footer, mark);
                footer_len = strlen(footer_mark_str);
                in_block = TRUE;
                desc_found = FALSE;
            }
            else
            {
                in_block = FALSE;
            }

            continue;
        }

        if (!in_block) continue;

        if (!desc_found && prefix(buf, "# desc: "))
        {
            const char *desc = buf + strlen("# desc: ");
            if (*desc)
            {
                strnfmt(slots[mark_slot].desc, sizeof(slots[mark_slot].desc), "%s", desc);
                desc_found = TRUE;
            }
            continue;
        }

        if (!strncmp(buf, footer_mark_str, footer_len))
        {
            in_block = FALSE;
            mark_slot = -1;
        }
    }

    my_fclose(fp);
}

static bool config_open_dump(cptr section, int slot, cptr desc)
{
    char path[1024];
    char mark[128];
    char timestamp[64];

    config_build_path(path, sizeof(path));
    config_build_mark(mark, sizeof(mark), section, slot);

    if (!open_auto_dump(path, mark)) return FALSE;

    config_timestamp(timestamp, sizeof(timestamp));
    if (desc && *desc)
        auto_dump_printf("# desc: %s | %s\n", desc, timestamp);
    else
        auto_dump_printf("# desc: %s | %s\n", player_name, timestamp);

    return TRUE;
}

static bool config_open_dump_slot(cptr section, int slot, cptr desc, cptr name_root)
{
    if (!config_open_dump(section, slot, desc)) return FALSE;

    if (slot == CONFIG_CURRENT_SLOT && name_root && *name_root)
        auto_dump_printf("# meta: name_root=%s\n", name_root);

    return TRUE;
}

static void config_remove_block(cptr section, int slot)
{
    char path[1024];
    char mark[128];
    cptr prev_mark = auto_dump_mark;

    config_build_path(path, sizeof(path));
    config_build_mark(mark, sizeof(mark), section, slot);

    auto_dump_mark = mark;
    remove_auto_dump(path);
    auto_dump_mark = prev_mark;
}

static bool config_for_each_line(cptr section, int slot, int (*cb)(cptr line, void *data), void *data)
{
    FILE *fp;
    char path[1024];
    char buf[1024];
    char mark[128];
    char target_mark[128];
    char footer_mark_str[128];
    size_t footer_len;
    bool in_block = FALSE;

    config_build_path(path, sizeof(path));
    fp = my_fopen(path, "r");
    if (!fp) return FALSE;

    config_build_mark(target_mark, sizeof(target_mark), section, slot);
    sprintf(footer_mark_str, auto_dump_footer, target_mark);
    footer_len = strlen(footer_mark_str);

    while (TRUE)
    {
        if (my_fgets(fp, buf, sizeof(buf))) break;

        if (config_parse_header_line(buf, mark, sizeof(mark)))
        {
            in_block = streq(mark, target_mark);
            continue;
        }

        if (!in_block) continue;

        if (!strncmp(buf, footer_mark_str, footer_len)) break;

        if (buf[0] == '#' || buf[0] == '\0') continue;

        if (cb && cb(buf, data) != 0)
        {
            my_fclose(fp);
            return FALSE;
        }
    }

    my_fclose(fp);
    return TRUE;
}

static int config_copy_line(cptr line, void *data)
{
    (void)data;
    auto_dump_printf("%s\n", line);
    return 0;
}

static bool config_copy_block(cptr section, int src_slot, int dst_slot, cptr desc)
{
    if (!config_open_dump(section, dst_slot, desc)) return FALSE;
    config_for_each_line(section, src_slot, config_copy_line, NULL);
    close_auto_dump();
    return TRUE;
}

void window_flag_dump(void)
{
    char name_root[80];
    config_name_root(name_root, sizeof(name_root), player_name);
    config_dump_window_flags_slot(CONFIG_CURRENT_SLOT, "Current Settings", name_root);
}


/*
 * Return suffix of ordinal number
 */
cptr get_ordinal_number_suffix(int num)
{
    num = ABS(num) % 100;
    switch (num % 10)
    {
    case 1:
        return (num == 11) ? "th" : "st";
    case 2:
        return (num == 12) ? "th" : "nd";
    case 3:
        return (num == 13) ? "th" : "rd";
    default:
        return "th";
    }
}

/*
 * Toggle easy_mimics
 */
void toggle_easy_mimics(bool kayta)
{
    int i;
    for (i = 1; i < max_r_idx; i++)
    {
        monster_race *r_ptr = &r_info[i];
        if (!r_ptr->name) continue;
        if (r_ptr->flags7 & RF7_NASTY_GLYPH)
        {
            if ((kayta) && (r_ptr->x_char == r_ptr->d_char)) r_ptr->x_char = 'x';
            else if ((!kayta) && (r_ptr->x_char == 'x')) r_ptr->x_char = r_ptr->d_char;
            if (r_ptr->d_attr == color_char_to_attr('d'))
            {
                if (kayta) r_ptr->x_attr = color_char_to_attr('D');
                else r_ptr->x_attr = color_char_to_attr('d');
            } 
        }
    }
}

/*
 * Calculate delay length
 */
int delay_time(void)
{
    if (square_delays) return (delay_factor * delay_factor * 2);
    else return (delay_factor * delay_factor * delay_factor);
}


/*
 * Hack -- redraw the screen
 *
 * This command performs various low level updates, clears all the "extra"
 * windows, does a total redraw of the main window, and requests all of the
 * interesting updates and redraws that I can think of.
 *
 * This command is also used to "instantiate" the results of the user
 * selecting various things, such as graphics mode, so it must call
 * the "TERM_XTRA_REACT" hook before redrawing the windows.
 */
bool redraw_hack;
void do_cmd_redraw(void)
{
    int j;

    term *old = Term;


    /* Hack -- react to changes */
    Term_xtra(TERM_XTRA_REACT, 0);


    /* Combine and Reorder the pack (later) */
    p_ptr->notice |= (PN_OPTIMIZE_PACK | PN_OPTIMIZE_QUIVER);


    /* Update torch */
    p_ptr->update |= (PU_TORCH);

    /* Update stuff */
    p_ptr->update |= (PU_BONUS | PU_HP | PU_MANA | PU_SPELLS);

    /* Forget lite/view */
    p_ptr->update |= (PU_UN_VIEW | PU_UN_LITE);

    /* Update lite/view */
    p_ptr->update |= (PU_VIEW | PU_LITE | PU_MON_LITE);

    /* Update monsters */
    p_ptr->update |= (PU_MONSTERS);

    /* Redraw everything */
    p_ptr->redraw |= (PR_WIPE | PR_BASIC | PR_EXTRA | PR_MAP | PR_EQUIPPY | PR_MSG_LINE);

    /* Window stuff */
    p_ptr->window |= (PW_INVEN | PW_EQUIP | PW_SPELL);

    /* Window stuff */
    p_ptr->window |= (PW_MESSAGE | PW_OVERHEAD | PW_DUNGEON |
        PW_MONSTER | PW_MONSTER_LIST | PW_OBJECT_LIST | PW_OBJECT);

    update_playtime();

    /* Prevent spamming ^R to circumvent fuzzy detection */
    redraw_hack = TRUE;
    handle_stuff();
    redraw_hack = FALSE;

    if (p_ptr->prace == RACE_ANDROID) android_calc_exp();


    /* Redraw every window */
    for (j = 0; j < 8; j++)
    {
        /* Dead window */
        if (!angband_term[j]) continue;

        /* Activate */
        Term_activate(angband_term[j]);

        /* Redraw */
        Term_redraw();

        /* Refresh */
        Term_fresh();

        /* Restore */
        Term_activate(old);
    }
}

/*
 * Show previous messages to the user    -BEN-
 *
 */
void do_cmd_messages(int old_now_turn)
{
    int     i;
    doc_ptr doc;
    int     current_turn = 0;
    int     current_row = 0;

    doc = doc_alloc(80);
    for (i = msg_count() - 1; i >= 0; i--)
    {
        msg_ptr m = msg_get(i);

        if (m->turn != current_turn)
        {
            if (doc_cursor(doc).y > current_row + 1)
                doc_newline(doc);
            current_turn = m->turn;
            current_row = doc_cursor(doc).y;
        }

        doc_insert_text(doc, m->color, string_buffer(m->msg));
        if (m->count > 1)
        {
            char buf[10];
            sprintf(buf, " (x%d)", m->count);
            doc_insert_text(doc, m->color, buf);
        }
        doc_newline(doc);
    }
    screen_save();
    doc_display(doc, "Previous Messages", doc_cursor(doc).y);
    screen_load();
    doc_free(doc);
}

#ifdef ALLOW_WIZARD

/*
 * Number of cheating options
 */
#define CHEAT_MAX 6

/*
 * Cheating options
 */
static option_type cheat_info[CHEAT_MAX] =
{
    { &cheat_peek,        FALSE,    255,    0x01, 0x00,
    "cheat_peek",        "Peek into object creation"
    },

    { &cheat_hear,        FALSE,    255,    0x02, 0x00,
    "cheat_hear",        "Peek into monster creation"
    },

    { &cheat_room,        FALSE,    255,    0x04, 0x00,
    "cheat_room",        "Peek into dungeon creation"
    },

    { &cheat_xtra,        FALSE,    255,    0x08, 0x00,
    "cheat_xtra",        "Peek into something else"
    },

    { &cheat_live,        FALSE,    255,    0x20, 0x00,
    "cheat_live",        "Allow player to avoid death"
    },

    { &cheat_save,        FALSE,    255,    0x40, 0x00,
    "cheat_save",        "Ask for saving death"
    }
};

/*
 * Interact with some options for cheating
 */
static void do_cmd_options_cheat(cptr info)
{
    int    ch;

    int        i, k = 0, n = CHEAT_MAX;

    char    buf[80];


    /* Clear screen */
    Term_clear();

    /* Interact with the player */
    while (TRUE)
    {
        int dir;

        /* Prompt XXX XXX XXX */
        sprintf(buf, "%s (RET to advance, y/n to set, ESC to accept) ", info);

        prt(buf, 0, 0);

        /* Display the options */
        for (i = 0; i < n; i++)
        {
            byte a = TERM_WHITE;

            /* Color current option */
            if (i == k) a = TERM_L_BLUE;

            /* Display the option text */
            sprintf(buf, "%-48s: %s (%s)",
                cheat_info[i].o_desc,
                (*cheat_info[i].o_var ? "yes" : "no "),

                cheat_info[i].o_text);
            c_prt(a, buf, i + 2, 0);
        }

        /* Hilite current option */
        move_cursor(k + 2, 50);

        autopick_inkey_hack = 1;

        /* Get a key */
        ch = inkey_special(TRUE);

        /*
         * HACK - Try to translate the key into a direction
         * to allow using the roguelike keys for navigation.
         */
        if (ch < 256)
        {
            dir = get_keymap_dir(ch, FALSE);
            if ((dir == 2) || (dir == 4) || (dir == 6) || (dir == 8) || (dir == 9) || (dir == 1))
                ch = I2D(dir);
        }

        /* Analyze */
        switch (ch)
        {
            case ESCAPE:
            {
                return;
            }

            case '-':
            case '8':
            case SKEY_UP:
            {
                k = (n + k - 1) % n;
                break;
            }

            case ' ':
            case '\n':
            case '\r':
            case '2':
            case SKEY_DOWN:
            {
                k = (k + 1) % n;
                break;
            }

            case SKEY_TOP:
            {
                k = MAX(0, k - 10);
                break;
            }

            case SKEY_BOTTOM:
            {
                k = MIN(n - 1, k + 10);
                break;
            }

            case 'y':
            case 'Y':
            case '6':
            case SKEY_RIGHT:
            {
                p_ptr->noscore |= (cheat_info[k].o_set * 256 + cheat_info[k].o_bit);
                (*cheat_info[k].o_var) = TRUE;
                k = (k + 1) % n;
                break;
            }

            case 'n':
            case 'N':
            case '4':
            case SKEY_LEFT:
            {
                (*cheat_info[k].o_var) = FALSE;
                k = (k + 1) % n;
                break;
            }

            case '?':
            {
                doc_display_help("option.txt", cheat_info[k].o_text);
                Term_clear();
                break;
            }

            default:
            {
                bell();
                break;
            }
        }
    }
}
#endif

static s16b toggle_frequency(s16b current)
{
    switch (current)
    {
    case 0: return 50;
    case 50: return 100;
    case 100: return 250;
    case 250: return 500;
    case 500: return 1000;
    case 1000: return 2500;
    case 2500: return 5000;
    case 5000: return 10000;
    case 10000: return 25000;
    default: return 0;
    }
}

static s16b toggle_frequency_back(s16b current)
{
    static const s16b freq_list[] = {0, 50, 100, 250, 500, 1000, 2500, 5000, 10000};
    size_t i;

    for (i = 0; i < sizeof(freq_list) / sizeof(freq_list[0]); i++)
    {
        if (freq_list[i] == current)
        {
            if (i == 0) return freq_list[sizeof(freq_list) / sizeof(freq_list[0]) - 1];
            return freq_list[i - 1];
        }
    }
    return 0;
}

#define OPTIONS_MAX 256

typedef enum
{
    OPT_ENTRY_OPTION = 0,
    OPT_ENTRY_SPECIAL = 1
} opt_entry_type_t;

typedef enum
{
    OPT_SPECIAL_DELAY_FACTOR = 0,
    OPT_SPECIAL_HITPOINT_WARN,
    OPT_SPECIAL_MANA_WARN,
    OPT_SPECIAL_AUTOSAVE_L,
    OPT_SPECIAL_AUTOSAVE_T,
    OPT_SPECIAL_AUTOSAVE_FREQ
} opt_special_type_t;

typedef struct opt_entry_s opt_entry_t;
struct opt_entry_s
{
    opt_entry_type_t type;
    int idx;
};

typedef struct options_snapshot_s options_snapshot_t;
struct options_snapshot_s
{
    bool opts[OPTIONS_MAX];
    int delay_factor;
    int hitpoint_warn;
    int mana_warn;
    int autosave_l;
    int autosave_t;
    int autosave_freq;
    int random_artifact_pct;
    int reduce_uniques_pct;
    int object_list_width;
    int monster_list_width;
    int generate_empty;
    int small_level_type;
    int pantheon_count;
    int game_pantheon;
};

static int options_count(void)
{
    int i;
    for (i = 0; option_info[i].o_desc; i++) ;
    return i;
}

static int option_index_by_var(bool *var)
{
    int i;
    for (i = 0; option_info[i].o_desc; i++)
    {
        if (option_info[i].o_var == var) return i;
    }
    return -1;
}

static int option_index_by_text(cptr text)
{
    int i;
    for (i = 0; option_info[i].o_desc; i++)
    {
        if (option_info[i].o_text && streq(option_info[i].o_text, text)) return i;
    }
    return -1;
}

#define DEFAULT_DELAY_FACTOR 2
#define DEFAULT_HITPOINT_WARN 5
#define DEFAULT_MANA_WARN 0
#define DEFAULT_AUTOSAVE_L 1
#define DEFAULT_AUTOSAVE_T 0
#define DEFAULT_AUTOSAVE_FREQ 0
#define DEFAULT_RANDOM_ARTIFACT_PCT 100
#define DEFAULT_REDUCE_UNIQUES_PCT 100
#define DEFAULT_OBJECT_LIST_WIDTH 50
#define DEFAULT_MONSTER_LIST_WIDTH 50
#define DEFAULT_GENERATE_EMPTY EMPTY_SOMETIMES
#define DEFAULT_SMALL_LEVEL_TYPE 0
#define DEFAULT_PANTHEON_COUNT 2
#define DEFAULT_GAME_PANTHEON 0

static void options_snapshot_current(options_snapshot_t *snap)
{
    int i;
    int cnt = options_count();

    for (i = 0; i < cnt; i++)
        snap->opts[i] = option_info[i].o_var ? *option_info[i].o_var : FALSE;

    snap->delay_factor = delay_factor;
    snap->hitpoint_warn = hitpoint_warn;
    snap->mana_warn = mana_warn;
    snap->autosave_l = autosave_l;
    snap->autosave_t = autosave_t;
    snap->autosave_freq = autosave_freq;
    snap->random_artifact_pct = random_artifact_pct;
    snap->reduce_uniques_pct = reduce_uniques_pct;
    snap->object_list_width = object_list_width;
    snap->monster_list_width = monster_list_width;
    snap->generate_empty = generate_empty;
    snap->small_level_type = small_level_type;
    snap->pantheon_count = pantheon_count;
    snap->game_pantheon = game_pantheon;
}

static void options_snapshot_defaults(options_snapshot_t *snap)
{
    int i;
    int cnt = options_count();

    for (i = 0; i < cnt; i++)
        snap->opts[i] = option_info[i].o_norm ? TRUE : FALSE;

    snap->delay_factor = DEFAULT_DELAY_FACTOR;
    snap->hitpoint_warn = DEFAULT_HITPOINT_WARN;
    snap->mana_warn = DEFAULT_MANA_WARN;
    snap->autosave_l = DEFAULT_AUTOSAVE_L;
    snap->autosave_t = DEFAULT_AUTOSAVE_T;
    snap->autosave_freq = DEFAULT_AUTOSAVE_FREQ;
    snap->random_artifact_pct = DEFAULT_RANDOM_ARTIFACT_PCT;
    snap->reduce_uniques_pct = DEFAULT_REDUCE_UNIQUES_PCT;
    snap->object_list_width = DEFAULT_OBJECT_LIST_WIDTH;
    snap->monster_list_width = DEFAULT_MONSTER_LIST_WIDTH;
    snap->generate_empty = DEFAULT_GENERATE_EMPTY;
    snap->small_level_type = DEFAULT_SMALL_LEVEL_TYPE;
    snap->pantheon_count = DEFAULT_PANTHEON_COUNT;
    snap->game_pantheon = DEFAULT_GAME_PANTHEON;
}

static void options_snapshot_apply(const options_snapshot_t *snap)
{
    int i;
    int cnt = options_count();

    for (i = 0; i < cnt; i++)
    {
        if (option_info[i].o_var)
            *option_info[i].o_var = snap->opts[i];
    }

    delay_factor = snap->delay_factor;
    hitpoint_warn = snap->hitpoint_warn;
    mana_warn = snap->mana_warn;
    autosave_l = snap->autosave_l;
    autosave_t = snap->autosave_t;
    autosave_freq = snap->autosave_freq;
    random_artifact_pct = snap->random_artifact_pct;
    reduce_uniques_pct = snap->reduce_uniques_pct;
    object_list_width = snap->object_list_width;
    monster_list_width = snap->monster_list_width;
    generate_empty = snap->generate_empty;
    small_level_type = snap->small_level_type;
    pantheon_count = snap->pantheon_count;
    game_pantheon = snap->game_pantheon;

    always_small_levels = (small_level_type != 0);
    single_pantheon = (pantheon_count == 1);
    guaranteed_pantheon = (game_pantheon > 0);
}

static void options_snapshot_apply_numeric(options_snapshot_t *snap, cptr name, int val)
{
    int idx;

    if (streq(name, "delay_factor"))
    {
        snap->delay_factor = MAX(0, MIN(9, val));
        return;
    }
    if (streq(name, "hitpoint_warn"))
    {
        snap->hitpoint_warn = MAX(0, MIN(9, val));
        return;
    }
    if (streq(name, "mana_warn"))
    {
        snap->mana_warn = MAX(0, MIN(9, val));
        return;
    }
    if (streq(name, "autosave_l"))
    {
        snap->autosave_l = (val != 0);
        return;
    }
    if (streq(name, "autosave_t"))
    {
        snap->autosave_t = (val != 0);
        return;
    }
    if (streq(name, "autosave_freq"))
    {
        snap->autosave_freq = MAX(0, val);
        return;
    }
    if (streq(name, "random_artifact_pct"))
    {
        snap->random_artifact_pct = MAX(0, MIN(100, val));
        idx = option_index_by_var(&random_artifacts);
        if (idx >= 0) snap->opts[idx] = (snap->random_artifact_pct > 0);
        return;
    }
    if (streq(name, "reduce_uniques_pct"))
    {
        snap->reduce_uniques_pct = MAX(0, MIN(100, val));
        idx = option_index_by_var(&reduce_uniques);
        if (idx >= 0) snap->opts[idx] = (snap->reduce_uniques_pct > 0);
        return;
    }
    if (streq(name, "object_list_width"))
    {
        snap->object_list_width = MAX(24, val);
        return;
    }
    if (streq(name, "monster_list_width"))
    {
        snap->monster_list_width = MAX(24, val);
        return;
    }
    if (streq(name, "generate_empty"))
    {
        snap->generate_empty = MAX(0, MIN(EMPTY_MAX - 1, val));
        idx = option_index_by_var(&ironman_empty_levels);
        if (idx >= 0) snap->opts[idx] = (snap->generate_empty == EMPTY_ALWAYS);
        return;
    }
    if (streq(name, "small_level_type"))
    {
        snap->small_level_type = MAX(0, MIN(SMALL_LVL_MAX, val));
        idx = option_index_by_var(&always_small_levels);
        if (idx >= 0) snap->opts[idx] = (snap->small_level_type != 0);
        return;
    }
    if (streq(name, "pantheon_count"))
    {
        snap->pantheon_count = MAX(1, MIN(PANTHEON_MAX - 1, val));
        idx = option_index_by_var(&single_pantheon);
        if (idx >= 0) snap->opts[idx] = (snap->pantheon_count == 1);
        return;
    }
    if (streq(name, "game_pantheon"))
    {
        snap->game_pantheon = MAX(0, MIN(PANTHEON_MAX - 1, val));
        idx = option_index_by_var(&guaranteed_pantheon);
        if (idx >= 0) snap->opts[idx] = (snap->game_pantheon > 0);
        return;
    }
}

static void options_snapshot_apply_line(options_snapshot_t *snap, cptr line)
{
    char *zz[3];
    char buf[1024];

    strnfmt(buf, sizeof(buf), "%s", line);

    if (buf[1] != ':') return;

    if ((buf[0] == 'X' || buf[0] == 'Y') &&
        tokenize(buf + 2, 1, zz, TOKENIZE_CHECKQUOTE) == 1)
    {
        int idx = option_index_by_text(zz[0]);
        if (idx >= 0)
            snap->opts[idx] = (buf[0] == 'Y');
        return;
    }

    if (buf[0] == 'O' &&
        tokenize(buf + 2, 2, zz, TOKENIZE_CHECKQUOTE) == 2)
    {
        options_snapshot_apply_numeric(snap, zz[0], (int)strtol(zz[1], NULL, 0));
        return;
    }
}

static void options_build_entries(int page, opt_entry_t *entries, int *entry_count)
{
    int i;
    int n = 0;

    for (i = 0; option_info[i].o_desc; i++)
    {
        if (option_info[i].o_page == page)
        {
            entries[n].type = OPT_ENTRY_OPTION;
            entries[n].idx = i;
            n++;
        }
    }

    switch (page)
    {
        case OPT_PAGE_MAPSCREEN:
            entries[n].type = OPT_ENTRY_SPECIAL;
            entries[n].idx = OPT_SPECIAL_DELAY_FACTOR;
            n++;
            break;
        case OPT_PAGE_TEXT:
            entries[n].type = OPT_ENTRY_SPECIAL;
            entries[n].idx = OPT_SPECIAL_MANA_WARN;
            n++;
            break;
        case OPT_PAGE_GAMEPLAY:
            entries[n].type = OPT_ENTRY_SPECIAL;
            entries[n].idx = OPT_SPECIAL_AUTOSAVE_L;
            n++;
            entries[n].type = OPT_ENTRY_SPECIAL;
            entries[n].idx = OPT_SPECIAL_AUTOSAVE_T;
            n++;
            entries[n].type = OPT_ENTRY_SPECIAL;
            entries[n].idx = OPT_SPECIAL_AUTOSAVE_FREQ;
            n++;
            break;
        case OPT_PAGE_DISTURBANCE:
            entries[n].type = OPT_ENTRY_SPECIAL;
            entries[n].idx = OPT_SPECIAL_HITPOINT_WARN;
            n++;
            break;
        default:
            break;
    }

    *entry_count = n;
}

static void options_format_special_value(opt_special_type_t type, const options_snapshot_t *snap, char *buf, size_t buf_len)
{
    switch (type)
    {
        case OPT_SPECIAL_DELAY_FACTOR:
        {
            int msec = snap ? (square_delays ? snap->delay_factor * snap->delay_factor * 2
                                            : snap->delay_factor * snap->delay_factor * snap->delay_factor)
                            : delay_time();
            int val = snap ? snap->delay_factor : delay_factor;
            strnfmt(buf, buf_len, "%d (%d ms)", val, msec);
            break;
        }
        case OPT_SPECIAL_HITPOINT_WARN:
            strnfmt(buf, buf_len, "%d0%%", snap ? snap->hitpoint_warn : hitpoint_warn);
            break;
        case OPT_SPECIAL_MANA_WARN:
            strnfmt(buf, buf_len, "%d0%%", snap ? snap->mana_warn : mana_warn);
            break;
        case OPT_SPECIAL_AUTOSAVE_L:
            strnfmt(buf, buf_len, "%s", (snap ? snap->autosave_l : autosave_l) ? "yes" : "no");
            break;
        case OPT_SPECIAL_AUTOSAVE_T:
            strnfmt(buf, buf_len, "%s", (snap ? snap->autosave_t : autosave_t) ? "yes" : "no");
            break;
        case OPT_SPECIAL_AUTOSAVE_FREQ:
        {
            int val = snap ? snap->autosave_freq : autosave_freq;
            if (val <= 0) strnfmt(buf, buf_len, "off");
            else strnfmt(buf, buf_len, "every %d turns", val);
            break;
        }
    }
}

static cptr options_special_desc(opt_special_type_t type)
{
    switch (type)
    {
        case OPT_SPECIAL_DELAY_FACTOR: return "Base Delay Factor";
        case OPT_SPECIAL_HITPOINT_WARN: return "Hitpoint Warning";
        case OPT_SPECIAL_MANA_WARN: return "Mana Color Threshold";
        case OPT_SPECIAL_AUTOSAVE_L: return "Autosave when entering new levels";
        case OPT_SPECIAL_AUTOSAVE_T: return "Timed autosave";
        case OPT_SPECIAL_AUTOSAVE_FREQ: return "Timed autosave frequency";
    }
    return "";
}

static cptr options_special_help(opt_special_type_t type)
{
    switch (type)
    {
        case OPT_SPECIAL_DELAY_FACTOR: return "BaseDelay";
        case OPT_SPECIAL_HITPOINT_WARN: return "Hitpoint";
        case OPT_SPECIAL_MANA_WARN: return "Hitpoint";
        case OPT_SPECIAL_AUTOSAVE_L: return "Autosave";
        case OPT_SPECIAL_AUTOSAVE_T: return "Autosave";
        case OPT_SPECIAL_AUTOSAVE_FREQ: return "Autosave";
    }
    return NULL;
}

static void config_options_load_page(int page);
static void config_options_save_page(int page);
static void config_options_reset_page(int page);
static void config_options_load_all(void);
static void config_options_save_all(void);
static void config_options_reset_all(void);
static void config_options_load_all_slot(int slot);
void config_birth_save(void);
void config_birth_load(bool allow_load);



/*
 * Interact with some options
 */
void do_cmd_options_aux(int page, cptr info)
{
    int     ch;
    int     i, k = 0, n = 0, l;
    opt_entry_t entries[OPTIONS_MAX + 8];
    char    buf[160];
    bool    browse_only = (page == OPT_PAGE_BIRTH) && character_generated &&
                          (!p_ptr->wizard || !allow_debug_opts);
    bool    scroll_mode;
    byte    option_offset = 0;
    byte    bottom_opt = Term->hgt - ((page == OPT_PAGE_AUTODESTROY) ? 5 : 2);
    options_snapshot_t options_before;

    options_build_entries(page, entries, &n);
    options_snapshot_current(&options_before);

    scroll_mode = (n > bottom_opt);

    /* Clear screen */
    Term_clear();

    /* Interact with the player */
    while (TRUE)
    {
        int dir;

        /* Prompt XXX XXX XXX */
        sprintf(buf, "%s (RET:next, %s, l:load, s:save, r:reset, ?:help) ",
            info, browse_only ? "ESC:exit" : "y/n:change, ESC:accept");

        prt(buf, 0, 0);

        /* HACK -- description for easy-auto-destroy options */
        if (page == OPT_PAGE_AUTODESTROY) c_prt(TERM_YELLOW, "Following options will protect items from easy auto-destroyer.", 11, 3);

        /* Display the options */
        for (i = option_offset; i < n; i++)
        {
            int rivi;
            byte a = TERM_WHITE;
            opt_entry_t *entry = &entries[i];

            /* Color current option */
            if (i == k) a = TERM_L_BLUE;

            if (entry->type == OPT_ENTRY_SPECIAL)
            {
                char valbuf[80];
                options_format_special_value(entry->idx, NULL, valbuf, sizeof(valbuf));
                sprintf(buf, "%-48s: %s", options_special_desc(entry->idx), valbuf);
            }
            else if (option_info[entry->idx].o_var == &random_artifacts)
            {
                sprintf(buf, "%-48s: ", option_info[entry->idx].o_desc);
                if (random_artifacts)
                    sprintf(buf + strlen(buf), "%d%% ", random_artifact_pct);
                else
                    strcat(buf, "no  ");
                sprintf(buf + strlen(buf), "(%.19s)", option_info[entry->idx].o_text);
            }
            else if (option_info[entry->idx].o_var == &ironman_empty_levels)
            {
                sprintf(buf, "%-48s: ", option_info[entry->idx].o_desc);
                sprintf(buf + strlen(buf), "%s", empty_lv_description[generate_empty]);
            }
            else if (option_info[entry->idx].o_var == &reduce_uniques)
            {
                sprintf(buf, "%-48s: ", option_info[entry->idx].o_desc);
                if (reduce_uniques)
                    sprintf(buf + strlen(buf), "%d%% ", reduce_uniques_pct);
                else
                    strcat(buf, "no  ");
                sprintf(buf + strlen(buf), "(%.19s)", option_info[entry->idx].o_text);
            }
            else if (option_info[entry->idx].o_var == &obj_list_width)
            {
                sprintf(buf, "%-48s: ", option_info[entry->idx].o_desc);
                sprintf(buf + strlen(buf), "%-3d ", object_list_width);
                sprintf(buf + strlen(buf), "(%.19s)", option_info[entry->idx].o_text);
            }
            else if (option_info[entry->idx].o_var == &mon_list_width)
            {
                sprintf(buf, "%-48s: ", option_info[entry->idx].o_desc);
                sprintf(buf + strlen(buf), "%-3d ", monster_list_width);
                sprintf(buf + strlen(buf), "(%.19s)", option_info[entry->idx].o_text);
            }
            else if (option_info[entry->idx].o_var == &single_pantheon)
            {
                sprintf(buf, "%-48s: ", option_info[entry->idx].o_desc);
                sprintf(buf + strlen(buf), "%d of %d", pantheon_count, PANTHEON_MAX - 1);
            }
            else if (option_info[entry->idx].o_var == &guaranteed_pantheon)
            {
                sprintf(buf, "%-48s: ", option_info[entry->idx].o_desc);
                if (pantheon_count == PANTHEON_MAX - 1)
                {
                    strcat(buf, "All ");
                }
                else if ((game_pantheon) && (game_pantheon < PANTHEON_MAX))
                {
                    sprintf(buf + strlen(buf), "%.3s ", pant_list[game_pantheon].short_name);
                }
                else
                    strcat(buf, "None");
            }
            else if (option_info[entry->idx].o_var == &always_small_levels)
            {
                sprintf(buf, "%-48s: ", option_info[entry->idx].o_desc);
                sprintf(buf + strlen(buf), "%s ", lv_size_options[small_level_type]);
            }
            else
            {
                sprintf(buf, "%-48s: %s (%.19s)",
                    option_info[entry->idx].o_desc,
                    (*option_info[entry->idx].o_var ? "yes" : "no "),
                    option_info[entry->idx].o_text);
            }
            if ((page == OPT_PAGE_AUTODESTROY) && i > 7) rivi = i + 5 - option_offset;
            else rivi = i + 2 - option_offset;
            if ((scroll_mode) && (rivi == Term->hgt - 1) && (i < n - 1)) c_prt(TERM_YELLOW, " (scroll down for more options)", rivi, 0);
            else if ((scroll_mode) && (rivi == 2) && (i > 0)) c_prt(TERM_YELLOW, " (scroll up for more options)", rivi, 0);
            else if (((rivi >= 2) && (rivi < Term->hgt - 1)) || ((rivi == Term->hgt - 1) && ((i == n - 1) || (!scroll_mode)))) c_prt(a, buf, rivi, 0);
        }

        if ((page == OPT_PAGE_AUTODESTROY) && (k > 7)) l = 3;
        else l = 0;

        /* Hilite current option */
        move_cursor(k + 2 + l - option_offset, 50);

        autopick_inkey_hack = 1;

        /* Get a key */
        ch = inkey_special(TRUE);

        /*
         * HACK - Try to translate the key into a direction
         * to allow using the roguelike keys for navigation.
         */
        if (ch < 256)
        {
            dir = get_keymap_dir(ch, FALSE);
            if ((dir == 2) || (dir == 4) || (dir == 6) || (dir == 8) || (dir == 9) || (dir == 1))
                ch = I2D(dir);
        }

        /* Analyze */
        switch (ch)
        {
            case ESCAPE:
            {
                if (!browse_only)
                {
                    options_snapshot_t cur_opts;
                    char diff_lines[512][160];
                    int diff_count = 0;

                    options_snapshot_current(&cur_opts);
                    options_diff_page(page, &options_before, &cur_opts, diff_lines, &diff_count, 512);
                    if (diff_count > 0)
                    {
                        while (1)
                        {
                            int resp;
                            prt("Save option changes to Current Settings? (y/n/r)", 0, 0);
                            resp = inkey();
                            if (resp == 'y' || resp == 'Y')
                            {
                                config_save_current_options_page(page);
                                msg_print("Saved to Current Settings.");
                                break;
                            }
                            if (resp == 'r' || resp == 'R')
                            {
                                options_snapshot_apply(&options_before);
                                msg_print("Reverted option changes.");
                                break;
                            }
                            if (resp == 'n' || resp == 'N' || resp == ESCAPE)
                                break;
                            bell();
                        }
                    }
                }
                return;
            }

            case 'l':
            case 'L':
            {
                if (browse_only) bell();
                else config_options_load_page(page);
                Term_clear();
                break;
            }

            case 's':
            case 'S':
            {
                if (browse_only) bell();
                else config_options_save_page(page);
                Term_clear();
                break;
            }

            case 'r':
            case 'R':
            {
                if (browse_only) bell();
                else config_options_reset_page(page);
                Term_clear();
                break;
            }

            case '-':
            case '8':
            case SKEY_UP:
            {
                k = (n + k - 1) % n;
                if (scroll_mode)
                {
                    if (k > bottom_opt - 1 + option_offset) option_offset = k - bottom_opt + 1;
                    else if ((k < option_offset) || ((k > 0) && (k == option_offset))) option_offset = MAX(0, k - 3);
                }
                break;
            }

            case ' ':
            case '\n':
            case '\r':
            case '2':
            case SKEY_DOWN:
            {
                k = (k + 1) % n;
                if (scroll_mode)
                {
                    if (k > bottom_opt - 2 + option_offset) option_offset = k - bottom_opt + ((k == n - 1) ? 1 : 2);
                    else if ((k < option_offset) || ((k > 0) && (k == option_offset))) option_offset = MAX(0, k - 3);
                }
                break;
            }

            case '7':
            case '9':
            case SKEY_PGUP:
            case SKEY_TOP:
            {
                k = MAX(0, k - 10);
                if (scroll_mode)
                {
                    if (k > bottom_opt - 1 + option_offset) option_offset = k - bottom_opt + 1;
                    else if ((k < option_offset) || ((k > 0) && (k == option_offset))) option_offset = MAX(0, k - 3);
                }
                break;
            }

            case '1':
            case '3':
            case SKEY_PGDOWN:
            case SKEY_BOTTOM:
            {
                k = MIN(n - 1, k + 10);
                if (scroll_mode)
                {
                    if (k > bottom_opt - 2 + option_offset) option_offset = k - bottom_opt + ((k == n - 1) ? 1 : 2);
                    else if ((k < option_offset) || ((k > 0) && (k == option_offset))) option_offset = MAX(0, k - 3);
                }
                break;
            }

            case 'y':
            case 'Y':
            case '6':
            case SKEY_RIGHT:
            {
                opt_entry_t *entry = &entries[k];

                if (browse_only) break;

                if (entry->type == OPT_ENTRY_SPECIAL)
                {
                    switch (entry->idx)
                    {
                        case OPT_SPECIAL_DELAY_FACTOR:
                            if (delay_factor < 9) delay_factor++;
                            break;
                        case OPT_SPECIAL_HITPOINT_WARN:
                            if (hitpoint_warn < 9) hitpoint_warn++;
                            break;
                        case OPT_SPECIAL_MANA_WARN:
                            if (mana_warn < 9) mana_warn++;
                            break;
                        case OPT_SPECIAL_AUTOSAVE_L:
                            autosave_l = TRUE;
                            break;
                        case OPT_SPECIAL_AUTOSAVE_T:
                            autosave_t = TRUE;
                            break;
                        case OPT_SPECIAL_AUTOSAVE_FREQ:
                            autosave_freq = toggle_frequency_back(autosave_freq);
                            break;
                    }
                    break;
                }

                if (option_info[entry->idx].o_var == &random_artifacts)
                {
                    if (!random_artifacts)
                    {
                        random_artifacts = TRUE;
                        random_artifact_pct = 10;
                    }
                    else
                    {
                        random_artifact_pct += 10;
                        if (random_artifact_pct > 100) random_artifacts = FALSE;
                    }
                }
                else if (option_info[entry->idx].o_var == &obj_list_width)
                {
                    int maksi = MAX(50, Term->wid - 15);
                    maksi &= ~(0x01);
                    object_list_width += 2;
                    if (object_list_width > maksi) object_list_width = maksi;
                }
                else if (option_info[entry->idx].o_var == &mon_list_width)
                {
                    int maksi = MAX(50, Term->wid - 15);
                    maksi &= ~(0x01);
                    monster_list_width += 2;
                    if (monster_list_width > maksi) monster_list_width = maksi;
                }
                else if (option_info[entry->idx].o_var == &reduce_uniques)
                {
                    if (!reduce_uniques)
                    {
                        reduce_uniques = TRUE;
                        reduce_uniques_pct = 10;
                    }
                    else
                    {
                        reduce_uniques_pct += 10;
                        if (reduce_uniques_pct >= 100) reduce_uniques = FALSE;
                    }
                }
                else if (option_info[entry->idx].o_var == &ironman_empty_levels)
                {
                    generate_empty++;
                    if (generate_empty == EMPTY_MAX) generate_empty = 0;
                    ironman_empty_levels = (generate_empty == EMPTY_ALWAYS);
                }
                else if (option_info[entry->idx].o_var == &single_pantheon)
                {
                    pantheon_count++;
                    if (pantheon_count >= PANTHEON_MAX) pantheon_count = 1;
                }
                else if (option_info[entry->idx].o_var == &guaranteed_pantheon)
                {
                    game_pantheon++;
                    if (game_pantheon >= PANTHEON_MAX) game_pantheon = 0;
                }
                else if (option_info[entry->idx].o_var == &always_small_levels)
                {
                    if (!always_small_levels)
                    {
                        always_small_levels = TRUE;
                        small_level_type = 1;
                    }
                    else
                    {
                        small_level_type++;
                        if (small_level_type > SMALL_LVL_MAX)
                        {
                            always_small_levels = FALSE;
                            small_level_type = 0;
                        }
                    }
                }
                else
                {
                    (*option_info[entry->idx].o_var) = TRUE;
                    k = (k + 1) % n;
                    if (scroll_mode)
                    {
                        if (k > bottom_opt - 2 + option_offset) option_offset = k - bottom_opt + ((k == n - 1) ? 1 : 2);
                        else if ((k < option_offset) || ((k > 0) && (k == option_offset))) option_offset = MAX(0, k - 3);
                    }
                }
                break;
            }

            case 'n':
            case 'N':
            case '4':
            case SKEY_LEFT:
            {
                opt_entry_t *entry = &entries[k];

                if (browse_only) break;

                if (entry->type == OPT_ENTRY_SPECIAL)
                {
                    switch (entry->idx)
                    {
                        case OPT_SPECIAL_DELAY_FACTOR:
                            if (delay_factor > 0) delay_factor--;
                            break;
                        case OPT_SPECIAL_HITPOINT_WARN:
                            if (hitpoint_warn > 0) hitpoint_warn--;
                            break;
                        case OPT_SPECIAL_MANA_WARN:
                            if (mana_warn > 0) mana_warn--;
                            break;
                        case OPT_SPECIAL_AUTOSAVE_L:
                            autosave_l = FALSE;
                            break;
                        case OPT_SPECIAL_AUTOSAVE_T:
                            autosave_t = FALSE;
                            break;
                        case OPT_SPECIAL_AUTOSAVE_FREQ:
                            autosave_freq = toggle_frequency(autosave_freq);
                            break;
                    }
                    break;
                }

                if (option_info[entry->idx].o_var == &random_artifacts)
                {
                    if (!random_artifacts)
                    {
                        random_artifacts = TRUE;
                        random_artifact_pct = 100;
                    }
                    else
                    {
                        random_artifact_pct -= 10;
                        if (random_artifact_pct <= 0) random_artifacts = FALSE;
                    }
                }
                else if (option_info[entry->idx].o_var == &reduce_uniques)
                {
                    if (!reduce_uniques)
                    {
                        reduce_uniques = TRUE;
                        reduce_uniques_pct = 90;
                    }
                    else
                    {
                        reduce_uniques_pct -= 10;
                        if (reduce_uniques_pct <= 0)
                        {
                            reduce_uniques = FALSE;
                            reduce_uniques_pct = 100;
                        }
                    }
                }
                else if (option_info[entry->idx].o_var == &obj_list_width)
                {
                    object_list_width -= 2;
                    if (object_list_width < 24) object_list_width = 24;
                }
                else if (option_info[entry->idx].o_var == &mon_list_width)
                {
                    monster_list_width -= 2;
                    if (monster_list_width < 24) monster_list_width = 24;
                }
                else if (option_info[entry->idx].o_var == &ironman_empty_levels)
                {
                    if (generate_empty == 0) generate_empty = EMPTY_MAX - 1;
                    else generate_empty--;
                    ironman_empty_levels = (generate_empty == EMPTY_ALWAYS);
                }
                else if (option_info[entry->idx].o_var == &single_pantheon)
                {
                    pantheon_count--;
                    if (pantheon_count < 1) pantheon_count = PANTHEON_MAX - 1;
                }
                else if (option_info[entry->idx].o_var == &guaranteed_pantheon)
                {
                    if (game_pantheon) game_pantheon--;
                    else game_pantheon = PANTHEON_MAX - 1;
                }
                else if (option_info[entry->idx].o_var == &always_small_levels)
                {
                    if (!always_small_levels)
                    {
                        always_small_levels = TRUE;
                        small_level_type = SMALL_LVL_MAX;
                    }
                    else
                    {
                        small_level_type--;
                        if (small_level_type == 0) always_small_levels = FALSE;
                    }
                }
                else
                {
                    (*option_info[entry->idx].o_var) = FALSE;
                    k = (k + 1) % n;
                    if (scroll_mode)
                    {
                        if (k > bottom_opt - 2 + option_offset) option_offset = k - bottom_opt + ((k == n - 1) ? 1 : 2);
                        else if ((k < option_offset) || ((k > 0) && (k == option_offset))) option_offset = MAX(0, k - 3);
                    }
                }
                break;
            }

            case 't':
            case 'T':
            {
                opt_entry_t *entry = &entries[k];

                if (browse_only) break;
                if (entry->type == OPT_ENTRY_OPTION)
                    (*option_info[entry->idx].o_var) = !(*option_info[entry->idx].o_var);
                break;
            }

            case '?':
            {
                opt_entry_t *entry = &entries[k];
                cptr help = NULL;
                if (entry->type == OPT_ENTRY_SPECIAL) help = options_special_help(entry->idx);
                else help = option_info[entry->idx].o_text;
                doc_display_help("option.txt", help);
                Term_clear();
                break;
            }

            default:
            {
                bell();
                break;
            }
        }
    }
}

static void config_options_section(int page, char *buf, size_t buf_len)
{
    strnfmt(buf, buf_len, "options-page-%d", page);
}

static void config_options_scan_all_slots(config_slot_info_t slots[CONFIG_MAX_SLOTS])
{
    int i;

    for (i = 0; i < CONFIG_MAX_SLOTS; i++)
    {
        slots[i].used = FALSE;
        slots[i].desc[0] = '\0';
    }

    for (i = 1; i <= 7; i++)
    {
        char section[32];
        config_slot_info_t page_slots[CONFIG_MAX_SLOTS];
        int j;

        config_options_section(i, section, sizeof(section));
        config_scan_slots(section, page_slots);

        for (j = CONFIG_FIRST_USER_SLOT; j <= CONFIG_LAST_USER_SLOT; j++)
        {
            if (page_slots[j].used && !slots[j].used)
            {
                slots[j].used = TRUE;
                strnfmt(slots[j].desc, sizeof(slots[j].desc), "%s", page_slots[j].desc);
            }
        }
    }
}

static bool config_any_slots_used(config_slot_info_t slots[CONFIG_MAX_SLOTS], bool only_all_slots)
{
    int i;
    for (i = CONFIG_FIRST_USER_SLOT; i <= CONFIG_LAST_USER_SLOT; i++)
    {
        if (only_all_slots && i < CONFIG_FIRST_ALL_SLOT) continue;
        if (slots[i].used) return TRUE;
    }
    return FALSE;
}

typedef void (*config_slot_preview_f)(int slot, void *data);

static void config_render_slot_menu(cptr title, config_slot_info_t slots[CONFIG_MAX_SLOTS],
    bool only_all_slots, bool allow_delete, bool allow_duplicate)
{
    int row = 2;
    int i;
    char footer[120];
    char range[8];
    char range_caps[8];
    int max_letter = 'a' + (CONFIG_LAST_USER_SLOT - CONFIG_FIRST_USER_SLOT);
    int all_start = 'a' + (CONFIG_FIRST_ALL_SLOT - CONFIG_FIRST_USER_SLOT);

    if (only_all_slots)
        strnfmt(range, sizeof(range), "%c-%c", all_start, max_letter);
    else
        strnfmt(range, sizeof(range), "a-%c", max_letter);

    if (only_all_slots)
        strnfmt(range_caps, sizeof(range_caps), "%c-%c", toupper(all_start), toupper(max_letter));
    else
        strnfmt(range_caps, sizeof(range_caps), "A-%c", toupper(max_letter));

    Term_clear();
    prt(title, 0, 0);

    for (i = CONFIG_FIRST_USER_SLOT; i <= CONFIG_LAST_USER_SLOT; i++)
    {
        char line[120];
        cptr desc = slots[i].used ? slots[i].desc : "(empty)";

        if (only_all_slots && i < CONFIG_FIRST_ALL_SLOT) continue;

        if (i >= CONFIG_FIRST_ALL_SLOT)
            strnfmt(line, sizeof(line), "(%c) %s (All Settings)", 'a' + (i - CONFIG_FIRST_USER_SLOT), desc);
        else
            strnfmt(line, sizeof(line), "(%c) %s", 'a' + (i - CONFIG_FIRST_USER_SLOT), desc);
        prt(line, row++, 2);
    }

    if (allow_delete && allow_duplicate)
        strnfmt(footer, sizeof(footer), "Select a slot (%s), %s to preview, '-' delete, '=' duplicate, or ESC.",
            range, range_caps);
    else if (allow_delete)
        strnfmt(footer, sizeof(footer), "Select a slot (%s), %s to preview, '-' delete, or ESC.",
            range, range_caps);
    else if (allow_duplicate)
        strnfmt(footer, sizeof(footer), "Select a slot (%s), %s to preview, '=' duplicate, or ESC.",
            range, range_caps);
    else
        strnfmt(footer, sizeof(footer), "Select a slot (%s), %s to preview, or ESC.",
            range, range_caps);
    prt(footer, Term->hgt - 1, 0);
}

static bool config_prompt_slot_ex(cptr title, config_slot_info_t slots[CONFIG_MAX_SLOTS],
    bool only_all_slots, bool require_used, bool allow_delete, bool allow_duplicate,
    config_slot_preview_f preview_cb, void *preview_data,
    int *slot_out, bool *delete_out, bool *duplicate_out)
{
    screen_save();
    config_render_slot_menu(title, slots, only_all_slots, allow_delete, allow_duplicate);

    while (1)
    {
        int ch = inkey();
        int slot;
        bool delete_mode = FALSE;
        bool duplicate_mode = FALSE;
        char range[8];
        int max_letter = 'a' + (CONFIG_LAST_USER_SLOT - CONFIG_FIRST_USER_SLOT);
        int all_start = 'a' + (CONFIG_FIRST_ALL_SLOT - CONFIG_FIRST_USER_SLOT);

        if (only_all_slots)
            strnfmt(range, sizeof(range), "%c-%c", all_start, max_letter);
        else
            strnfmt(range, sizeof(range), "a-%c", max_letter);

        if (ch == ESCAPE)
        {
            screen_load();
            return FALSE;
        }

        if ((ch == '-' || ch == '_') && allow_delete)
        {
            delete_mode = TRUE;
            prt(format("Delete which slot (%s) or ESC to cancel.", range), Term->hgt - 2, 0);
            ch = inkey();
            if (ch == ESCAPE)
            {
                screen_load();
                return FALSE;
            }
        }

        if ((ch == '=' || ch == '+') && allow_duplicate)
        {
            duplicate_mode = TRUE;
            prt(format("Duplicate which slot (%s) or ESC to cancel.", range), Term->hgt - 2, 0);
            ch = inkey();
            if (ch == ESCAPE)
            {
                screen_load();
                return FALSE;
            }
        }

        if (!isalpha(ch))
        {
            bell();
            continue;
        }

        if (isupper(ch) && !delete_mode && !duplicate_mode)
        {
            slot = (tolower(ch) - 'a') + CONFIG_FIRST_USER_SLOT;
            if (slot < CONFIG_FIRST_USER_SLOT || slot > CONFIG_LAST_USER_SLOT)
            {
                bell();
                continue;
            }
            if (only_all_slots && slot < CONFIG_FIRST_ALL_SLOT)
            {
                bell();
                continue;
            }
            if (preview_cb)
                preview_cb(slot, preview_data);
            config_render_slot_menu(title, slots, only_all_slots, allow_delete, allow_duplicate);
            continue;
        }

        slot = (tolower(ch) - 'a') + CONFIG_FIRST_USER_SLOT;
        if (slot < CONFIG_FIRST_USER_SLOT || slot > CONFIG_LAST_USER_SLOT)
        {
            bell();
            continue;
        }
        if (only_all_slots && slot < CONFIG_FIRST_ALL_SLOT)
        {
            bell();
            continue;
        }
        if (require_used && !slots[slot].used)
        {
            bell();
            continue;
        }

        if (delete_out)
            *delete_out = delete_mode;
        if (duplicate_out)
            *duplicate_out = duplicate_mode;
        *slot_out = slot;
        screen_load();
        return TRUE;
    }
}

static bool config_prompt_slot(cptr title, config_slot_info_t slots[CONFIG_MAX_SLOTS],
    bool only_all_slots, bool require_used, int *slot_out)
{
    return config_prompt_slot_ex(title, slots, only_all_slots, require_used, FALSE, FALSE, NULL, NULL,
        slot_out, NULL, NULL);
}

static bool config_prompt_description(char *buf, size_t buf_len)
{
    strnfmt(buf, buf_len, "%s", player_name);
    if (!get_string("Description (optional): ", buf, buf_len)) return FALSE;
    if (!buf[0]) strnfmt(buf, buf_len, "%s", player_name);
    return TRUE;
}

static bool config_prompt_description_with_default(char *buf, size_t buf_len, cptr def)
{
    strnfmt(buf, buf_len, "%s", def && *def ? def : player_name);
    if (!get_string("Description (optional): ", buf, buf_len)) return FALSE;
    if (!buf[0]) strnfmt(buf, buf_len, "%s", def && *def ? def : player_name);
    return TRUE;
}

static void config_trim_right(char *buf)
{
    size_t len = strlen(buf);
    while (len > 0 && isspace((unsigned char)buf[len - 1]))
        buf[--len] = '\0';
}

static void config_name_root(char *buf, size_t buf_len, cptr name)
{
    char tmp[80];
    int pos = -1;

    strnfmt(tmp, sizeof(tmp), "%s", name ? name : "");
    config_trim_right(tmp);

    if (find_roman_numeral(tmp, &pos) > 0 && pos > 0)
    {
        tmp[pos] = '\0';
        config_trim_right(tmp);
    }
    else if (find_arabic_numeral(tmp, &pos) > 0 && pos > 0)
    {
        tmp[pos - 1] = '\0';
        config_trim_right(tmp);
    }

    if (!tmp[0])
        strnfmt(tmp, sizeof(tmp), "%s", name ? name : "");
    config_trim_right(tmp);

    strnfmt(buf, buf_len, "%s", tmp);
}

static bool config_read_meta_name_root(cptr section, int slot, char *buf, size_t buf_len)
{
    FILE *fp;
    char path[1024];
    char line[1024];
    char mark[128];
    char target_mark[128];
    char footer_mark_str[128];
    size_t footer_len;
    bool in_block = FALSE;

    config_build_path(path, sizeof(path));
    fp = my_fopen(path, "r");
    if (!fp) return FALSE;

    config_build_mark(target_mark, sizeof(target_mark), section, slot);
    sprintf(footer_mark_str, auto_dump_footer, target_mark);
    footer_len = strlen(footer_mark_str);

    while (TRUE)
    {
        if (my_fgets(fp, line, sizeof(line))) break;

        if (config_parse_header_line(line, mark, sizeof(mark)))
        {
            in_block = streq(mark, target_mark);
            continue;
        }

        if (!in_block) continue;
        if (!strncmp(line, footer_mark_str, footer_len)) break;

        if (prefix(line, "# meta: name_root="))
        {
            const char *root = line + strlen("# meta: name_root=");
            strnfmt(buf, buf_len, "%s", root);
            config_trim_right(buf);
            my_fclose(fp);
            return TRUE;
        }
    }

    my_fclose(fp);
    return FALSE;
}

static bool config_current_settings_name_root(char *buf, size_t buf_len)
{
    const char *sections[] = {
        "options-page-1",
        "macros",
        "visuals",
        "keymaps-rogue",
        "keymaps-orig",
        "window-flags",
        "birth",
        NULL
    };
    int i;
    for (i = 0; sections[i]; i++)
    {
        config_slot_info_t slots[CONFIG_MAX_SLOTS];
        config_scan_slots(sections[i], slots);
        if (!slots[CONFIG_CURRENT_SLOT].used) continue;
        if (config_read_meta_name_root(sections[i], CONFIG_CURRENT_SLOT, buf, buf_len))
            return TRUE;
    }
    return FALSE;
}

static void config_save_current_options_page(int page)
{
    char name_root[80];
    config_name_root(name_root, sizeof(name_root), player_name);
    config_dump_options_page_slot(page, CONFIG_CURRENT_SLOT, "Current Settings", name_root);
}

static void config_save_current_macros(void)
{
    char name_root[80];
    config_name_root(name_root, sizeof(name_root), player_name);
    config_dump_macros_slot(CONFIG_CURRENT_SLOT, "Current Settings", name_root);
}

static void config_save_current_keymaps(int mode)
{
    char name_root[80];
    config_name_root(name_root, sizeof(name_root), player_name);
    config_dump_keymaps_slot(CONFIG_CURRENT_SLOT, mode, "Current Settings", name_root);
}

static void config_save_current_visuals(void)
{
    char name_root[80];
    config_name_root(name_root, sizeof(name_root), player_name);
    config_dump_visuals_slot(CONFIG_CURRENT_SLOT, "Current Settings", name_root);
}

static void config_save_current_window_flags(void)
{
    char name_root[80];
    config_name_root(name_root, sizeof(name_root), player_name);
    config_dump_window_flags_slot(CONFIG_CURRENT_SLOT, "Current Settings", name_root);
}

static void config_save_current_birth(void)
{
    char name_root[80];
    config_name_root(name_root, sizeof(name_root), player_name);
    config_dump_birth_slot(CONFIG_CURRENT_SLOT, "Current Settings", name_root);
}

static void config_save_current_all_settings(void)
{
    char name_root[80];
    config_name_root(name_root, sizeof(name_root), player_name);
    config_dump_all_settings_slot(CONFIG_CURRENT_SLOT, "Current Settings", name_root);
}


static void options_format_bool(char *buf, size_t buf_len, bool val)
{
    strnfmt(buf, buf_len, "%s", val ? "yes" : "no");
}

static void options_format_pct(char *buf, size_t buf_len, int val)
{
    if (val <= 0) strnfmt(buf, buf_len, "no");
    else strnfmt(buf, buf_len, "%d%%", val);
}

static void options_format_autosave_freq(char *buf, size_t buf_len, int val)
{
    if (val <= 0) strnfmt(buf, buf_len, "off");
    else strnfmt(buf, buf_len, "every %d turns", val);
}

static void options_diff_add(char lines[][160], int *count, int max, cptr name, cptr old_val, cptr new_val)
{
    if (*count >= max) return;
    strnfmt(lines[*count], 160, "%-38s: %s -> %s", name, old_val, new_val);
    (*count)++;
}

static void options_diff_page(int page, const options_snapshot_t *cur, const options_snapshot_t *next,
    char lines[][160], int *count, int max)
{
    opt_entry_t entries[OPTIONS_MAX + 8];
    int n = 0;
    int i;

    options_build_entries(page, entries, &n);

    for (i = 0; i < n; i++)
    {
        opt_entry_t *entry = &entries[i];
        char old_buf[80];
        char new_buf[80];

        if (entry->type == OPT_ENTRY_SPECIAL)
        {
            int old_val = 0;
            int new_val = 0;
            switch (entry->idx)
            {
                case OPT_SPECIAL_DELAY_FACTOR:
                    old_val = cur->delay_factor;
                    new_val = next->delay_factor;
                    break;
                case OPT_SPECIAL_HITPOINT_WARN:
                    old_val = cur->hitpoint_warn;
                    new_val = next->hitpoint_warn;
                    break;
                case OPT_SPECIAL_MANA_WARN:
                    old_val = cur->mana_warn;
                    new_val = next->mana_warn;
                    break;
                case OPT_SPECIAL_AUTOSAVE_L:
                    old_val = cur->autosave_l;
                    new_val = next->autosave_l;
                    break;
                case OPT_SPECIAL_AUTOSAVE_T:
                    old_val = cur->autosave_t;
                    new_val = next->autosave_t;
                    break;
                case OPT_SPECIAL_AUTOSAVE_FREQ:
                    old_val = cur->autosave_freq;
                    new_val = next->autosave_freq;
                    break;
            }

            if (old_val == new_val) continue;

            if (entry->idx == OPT_SPECIAL_AUTOSAVE_FREQ)
            {
                options_format_autosave_freq(old_buf, sizeof(old_buf), old_val);
                options_format_autosave_freq(new_buf, sizeof(new_buf), new_val);
            }
            else if (entry->idx == OPT_SPECIAL_AUTOSAVE_L || entry->idx == OPT_SPECIAL_AUTOSAVE_T)
            {
                options_format_bool(old_buf, sizeof(old_buf), old_val != 0);
                options_format_bool(new_buf, sizeof(new_buf), new_val != 0);
            }
            else if (entry->idx == OPT_SPECIAL_DELAY_FACTOR)
            {
                strnfmt(old_buf, sizeof(old_buf), "%d", old_val);
                strnfmt(new_buf, sizeof(new_buf), "%d", new_val);
            }
            else
            {
                strnfmt(old_buf, sizeof(old_buf), "%d0%%", old_val);
                strnfmt(new_buf, sizeof(new_buf), "%d0%%", new_val);
            }
            options_diff_add(lines, count, max, options_special_desc(entry->idx), old_buf, new_buf);
            continue;
        }

        if (option_info[entry->idx].o_var == &random_artifacts)
        {
            if (cur->random_artifact_pct == next->random_artifact_pct) continue;
            options_format_pct(old_buf, sizeof(old_buf), cur->random_artifact_pct);
            options_format_pct(new_buf, sizeof(new_buf), next->random_artifact_pct);
            options_diff_add(lines, count, max, option_info[entry->idx].o_desc, old_buf, new_buf);
            continue;
        }
        if (option_info[entry->idx].o_var == &reduce_uniques)
        {
            if (cur->reduce_uniques_pct == next->reduce_uniques_pct) continue;
            options_format_pct(old_buf, sizeof(old_buf), cur->reduce_uniques_pct);
            options_format_pct(new_buf, sizeof(new_buf), next->reduce_uniques_pct);
            options_diff_add(lines, count, max, option_info[entry->idx].o_desc, old_buf, new_buf);
            continue;
        }
        if (option_info[entry->idx].o_var == &obj_list_width)
        {
            if (cur->object_list_width == next->object_list_width) continue;
            strnfmt(old_buf, sizeof(old_buf), "%d", cur->object_list_width);
            strnfmt(new_buf, sizeof(new_buf), "%d", next->object_list_width);
            options_diff_add(lines, count, max, option_info[entry->idx].o_desc, old_buf, new_buf);
            continue;
        }
        if (option_info[entry->idx].o_var == &mon_list_width)
        {
            if (cur->monster_list_width == next->monster_list_width) continue;
            strnfmt(old_buf, sizeof(old_buf), "%d", cur->monster_list_width);
            strnfmt(new_buf, sizeof(new_buf), "%d", next->monster_list_width);
            options_diff_add(lines, count, max, option_info[entry->idx].o_desc, old_buf, new_buf);
            continue;
        }
        if (option_info[entry->idx].o_var == &ironman_empty_levels)
        {
            if (cur->generate_empty == next->generate_empty) continue;
            strnfmt(old_buf, sizeof(old_buf), "%s", empty_lv_description[cur->generate_empty]);
            strnfmt(new_buf, sizeof(new_buf), "%s", empty_lv_description[next->generate_empty]);
            options_diff_add(lines, count, max, option_info[entry->idx].o_desc, old_buf, new_buf);
            continue;
        }
        if (option_info[entry->idx].o_var == &always_small_levels)
        {
            if (cur->small_level_type == next->small_level_type) continue;
            strnfmt(old_buf, sizeof(old_buf), "%s", lv_size_options[cur->small_level_type]);
            strnfmt(new_buf, sizeof(new_buf), "%s", lv_size_options[next->small_level_type]);
            options_diff_add(lines, count, max, option_info[entry->idx].o_desc, old_buf, new_buf);
            continue;
        }
        if (option_info[entry->idx].o_var == &single_pantheon)
        {
            if (cur->pantheon_count == next->pantheon_count) continue;
            strnfmt(old_buf, sizeof(old_buf), "%d of %d", cur->pantheon_count, PANTHEON_MAX - 1);
            strnfmt(new_buf, sizeof(new_buf), "%d of %d", next->pantheon_count, PANTHEON_MAX - 1);
            options_diff_add(lines, count, max, option_info[entry->idx].o_desc, old_buf, new_buf);
            continue;
        }
        if (option_info[entry->idx].o_var == &guaranteed_pantheon)
        {
            if (cur->game_pantheon == next->game_pantheon) continue;
            if (cur->pantheon_count == PANTHEON_MAX - 1)
                strnfmt(old_buf, sizeof(old_buf), "All");
            else if (cur->game_pantheon > 0)
                strnfmt(old_buf, sizeof(old_buf), "%s", pant_list[cur->game_pantheon].short_name);
            else
                strnfmt(old_buf, sizeof(old_buf), "None");
            if (next->pantheon_count == PANTHEON_MAX - 1)
                strnfmt(new_buf, sizeof(new_buf), "All");
            else if (next->game_pantheon > 0)
                strnfmt(new_buf, sizeof(new_buf), "%s", pant_list[next->game_pantheon].short_name);
            else
                strnfmt(new_buf, sizeof(new_buf), "None");
            options_diff_add(lines, count, max, option_info[entry->idx].o_desc, old_buf, new_buf);
            continue;
        }

        if (cur->opts[entry->idx] == next->opts[entry->idx]) continue;
        options_format_bool(old_buf, sizeof(old_buf), cur->opts[entry->idx]);
        options_format_bool(new_buf, sizeof(new_buf), next->opts[entry->idx]);
        options_diff_add(lines, count, max, option_info[entry->idx].o_desc, old_buf, new_buf);
    }
}

static char config_prompt_diff(cptr title, char lines[][160], int count, bool allow_all, bool confirm)
{
    int row = 1;
    int i;
    char prompt[80];

    screen_save();
    Term_clear();
    prt(title, 0, 0);

    if (count == 0)
    {
        prt("No changes detected.", 2, 0);
    }
    else
    {
        for (i = 0; i < count; i++)
        {
            if (row >= Term->hgt - 1)
            {
                prt("-more-", Term->hgt - 1, 0);
                inkey();
                Term_clear();
                prt(title, 0, 0);
                row = 1;
            }
            prt(lines[i], row++, 0);
        }
    }

    if (!confirm)
    {
        prt("Press ESC to return.", Term->hgt - 1, 0);
        while (1)
        {
            int ch = inkey();
            if (ch == ESCAPE) break;
        }
        screen_load();
        return 'n';
    }

    if (allow_all)
        strnfmt(prompt, sizeof(prompt), "Apply these settings? (y/n/A)");
    else
        strnfmt(prompt, sizeof(prompt), "Apply these settings? (y/n)");
    prt(prompt, Term->hgt - 1, 0);

    while (1)
    {
        int ch = inkey();
        if (ch == 'y' || ch == 'Y')
        {
            screen_load();
            return 'y';
        }
        if (ch == 'n' || ch == 'N' || ch == ESCAPE)
        {
            screen_load();
            return 'n';
        }
        if (allow_all && (ch == 'a' || ch == 'A'))
        {
            screen_load();
            return 'a';
        }
    }
}

typedef struct config_options_preview_s config_options_preview_t;
struct config_options_preview_s
{
    int page;
};

static void config_preview_options_page(int slot, void *data)
{
    config_options_preview_t *preview = (config_options_preview_t *)data;
    char section[32];
    config_slot_info_t slots[CONFIG_MAX_SLOTS];
    options_snapshot_t cur;
    options_snapshot_t next;
    char diff_lines[512][160];
    int diff_count = 0;

    if (!preview) return;

    config_options_section(preview->page, section, sizeof(section));
    config_scan_slots(section, slots);
    if (!slots[slot].used)
    {
        msg_print("Slot is empty.");
        return;
    }

    options_snapshot_current(&cur);
    next = cur;
    config_for_each_line(section, slot, config_apply_snapshot_line, &next);
    options_diff_page(preview->page, &cur, &next, diff_lines, &diff_count, 512);
    config_prompt_diff("Option Preview", diff_lines, diff_count, FALSE, FALSE);
}

static void config_preview_options_all(int slot, void *data)
{
    config_slot_info_t slots[CONFIG_MAX_SLOTS];
    options_snapshot_t cur;
    options_snapshot_t next;
    char diff_lines[512][160];
    int diff_count = 0;
    int page;

    (void)data;
    config_options_scan_all_slots(slots);
    if (!slots[slot].used)
    {
        msg_print("Slot is empty.");
        return;
    }

    options_snapshot_current(&cur);
    next = cur;
    for (page = 1; page <= 7; page++)
    {
        char section[32];
        config_options_section(page, section, sizeof(section));
        config_for_each_line(section, slot, config_apply_snapshot_line, &next);
    }

    for (page = 1; page <= 7; page++)
        options_diff_page(page, &cur, &next, diff_lines, &diff_count, 512);

    config_prompt_diff("All Settings Preview", diff_lines, diff_count, FALSE, FALSE);
}

typedef struct config_diff_entry_s config_diff_entry_t;
struct config_diff_entry_s
{
    char label[80];
    char old_val[120];
    char new_val[120];
    char detail1[160];
    char detail2[160];
};

static bool config_prompt_diff_list(cptr title, config_diff_entry_t *entries, int count, bool confirm)
{
    int top = 0;
    int cur = 0;
    int rows = Term->hgt - 5;

    if (count <= 0)
    {
        if (!confirm)
        {
            msg_print("No differences found.");
            return FALSE;
        }
        return get_check("No differences found. Load anyway? ");
    }

    screen_save();
    while (1)
    {
        int i;
        int row = 2;
        int ch;

        Term_clear();
        prt(title, 0, 0);
        if (confirm)
            prt("Use arrow keys to review changes, 'y' to confirm, ESC to cancel.", 1, 0);
        else
            prt("Use arrow keys to review changes, ESC to return.", 1, 0);

        if (cur < top) top = cur;
        if (cur >= top + rows) top = cur - rows + 1;

        for (i = top; i < count && i < top + rows; i++)
        {
            byte a = (i == cur) ? TERM_L_BLUE : TERM_WHITE;
            c_prt(a, entries[i].label, row++, 2);
        }

        if (entries[cur].detail1[0])
            prt(entries[cur].detail1, Term->hgt - 2, 0);
        else
            prt(format("Old: %s", entries[cur].old_val), Term->hgt - 2, 0);

        if (entries[cur].detail2[0])
            prt(entries[cur].detail2, Term->hgt - 1, 0);
        else
            prt(format("New: %s", entries[cur].new_val), Term->hgt - 1, 0);

        ch = inkey_special(TRUE);
        switch (ch)
        {
            case ESCAPE:
                screen_load();
                return FALSE;
            case 'n':
            case 'N':
                if (confirm)
                {
                    screen_load();
                    return FALSE;
                }
                bell();
                break;
            case 'y':
            case 'Y':
                if (confirm)
                {
                    screen_load();
                    return TRUE;
                }
                bell();
                break;
            case '8':
            case SKEY_UP:
                if (cur > 0) cur--;
                break;
            case '2':
            case SKEY_DOWN:
                if (cur < count - 1) cur++;
                break;
            case '7':
            case SKEY_PGUP:
            case SKEY_TOP:
                cur = MAX(0, cur - rows);
                break;
            case '1':
            case '3':
            case SKEY_PGDOWN:
            case SKEY_BOTTOM:
                cur = MIN(count - 1, cur + rows);
                break;
            default:
                bell();
                break;
        }
    }
}

typedef struct macro_snapshot_s macro_snapshot_t;
struct macro_snapshot_s
{
    int count;
    cptr pat[MACRO_MAX];
    cptr act[MACRO_MAX];
};

static void macro_snapshot_free(macro_snapshot_t *snap)
{
    int i;
    for (i = 0; i < snap->count; i++)
    {
        z_string_free((char *)snap->pat[i]);
        z_string_free((char *)snap->act[i]);
    }
    snap->count = 0;
}

static void macro_snapshot_add(macro_snapshot_t *snap, cptr pat, cptr act)
{
    if (!pat || !act) return;
    if (streq(pat, act)) return;
    if (snap->count >= MACRO_MAX) return;
    snap->pat[snap->count] = z_string_make(pat);
    snap->act[snap->count] = z_string_make(act);
    snap->count++;
}

static int macro_snapshot_find(const macro_snapshot_t *snap, cptr pat)
{
    int i;
    for (i = 0; i < snap->count; i++)
    {
        if (streq(snap->pat[i], pat)) return i;
    }
    return -1;
}

static void macro_snapshot_current(macro_snapshot_t *snap)
{
    int i;
    snap->count = 0;
    for (i = 0; i < macro__num; i++)
    {
        macro_snapshot_add(snap, macro__pat[i], macro__act[i]);
    }
}

static void macro_snapshot_apply(const macro_snapshot_t *snap)
{
    int i;
    macro_clear_all();
    for (i = 0; i < snap->count; i++)
        macro_add((char *)snap->pat[i], (char *)snap->act[i]);
}

typedef struct macro_parse_state_s macro_parse_state_t;
struct macro_parse_state_s
{
    macro_snapshot_t *snap;
    char action[1024];
    bool have_action;
};

static int macro_snapshot_apply_line(cptr line, void *data)
{
    macro_parse_state_t *state = (macro_parse_state_t *)data;
    char buf[1024];
    char tmp[1024];

    strnfmt(buf, sizeof(buf), "%s", line);
    if (buf[1] != ':') return 0;

    if (buf[0] == 'A')
    {
        text_to_ascii(state->action, buf + 2);
        state->have_action = TRUE;
        return 0;
    }

    if (buf[0] == 'P' && state->have_action)
    {
        text_to_ascii(tmp, buf + 2);
        macro_snapshot_add(state->snap, tmp, state->action);
        return 0;
    }

    return 0;
}

static void macro_snapshot_from_file(macro_snapshot_t *snap, cptr section, int slot)
{
    macro_parse_state_t state;
    snap->count = 0;
    state.snap = snap;
    state.have_action = FALSE;
    state.action[0] = '\0';
    config_for_each_line(section, slot, macro_snapshot_apply_line, &state);
}

typedef struct keymap_snapshot_s keymap_snapshot_t;
struct keymap_snapshot_s
{
    int mode;
    cptr act[256];
};

static void keymap_snapshot_free(keymap_snapshot_t *snap)
{
    int i;
    for (i = 0; i < 256; i++)
    {
        z_string_free((char *)snap->act[i]);
        snap->act[i] = NULL;
    }
}

static void keymap_snapshot_current(keymap_snapshot_t *snap, int mode)
{
    int i;
    snap->mode = mode;
    for (i = 0; i < 256; i++)
    {
        if (keymap_act[mode][i])
            snap->act[i] = z_string_make(keymap_act[mode][i]);
        else
            snap->act[i] = NULL;
    }
}

static void keymap_snapshot_apply(const keymap_snapshot_t *snap)
{
    int i;
    int mode = snap->mode;
    keymap_clear_mode(mode);
    for (i = 0; i < 256; i++)
    {
        if (snap->act[i])
            keymap_act[mode][i] = z_string_make(snap->act[i]);
    }
}

typedef struct keymap_parse_state_s keymap_parse_state_t;
struct keymap_parse_state_s
{
    keymap_snapshot_t *snap;
    char action[1024];
    bool have_action;
};

static int keymap_snapshot_apply_line(cptr line, void *data)
{
    keymap_parse_state_t *state = (keymap_parse_state_t *)data;
    char buf[1024];
    char tmp[1024];
    char *zz[3];
    int mode;
    int key;

    strnfmt(buf, sizeof(buf), "%s", line);
    if (buf[1] != ':') return 0;

    if (buf[0] == 'A')
    {
        text_to_ascii(state->action, buf + 2);
        state->have_action = TRUE;
        return 0;
    }

    if (buf[0] == 'C' && state->have_action)
    {
        if (tokenize(buf + 2, 2, zz, TOKENIZE_CHECKQUOTE) != 2) return 0;
        mode = strtol(zz[0], NULL, 0);
        if (mode != state->snap->mode) return 0;
        text_to_ascii(tmp, zz[1]);
        if (!tmp[0] || tmp[1]) return 0;
        key = (byte)tmp[0];
        z_string_free(state->snap->act[key]);
        state->snap->act[key] = z_string_make(state->action);
        return 0;
    }

    return 0;
}

static void keymap_snapshot_from_file(keymap_snapshot_t *snap, cptr section, int slot, int mode)
{
    keymap_parse_state_t state;
    keymap_snapshot_free(snap);
    snap->mode = mode;
    state.snap = snap;
    state.have_action = FALSE;
    state.action[0] = '\0';
    config_for_each_line(section, slot, keymap_snapshot_apply_line, &state);
}

static void keymap_clear_mode(int mode)
{
    int i;
    for (i = 0; i < 256; i++)
    {
        z_string_free(keymap_act[mode][i]);
        keymap_act[mode][i] = NULL;
    }
}


static int config_apply_pref_line(cptr line, void *data)
{
    (void)data;
    return process_pref_file_command((char *)line);
}

static int config_apply_pref_line_no_birth(cptr line, void *data)
{
    (void)data;
    if (line[0] == 'B' && line[1] == ':') return 0;
    return process_pref_file_command((char *)line);
}

static int config_apply_snapshot_line(cptr line, void *data)
{
    options_snapshot_t *snap = (options_snapshot_t *)data;
    options_snapshot_apply_line(snap, line);
    return 0;
}

static void config_dump_options_page(int page)
{
    int i;

    for (i = 0; option_info[i].o_desc; i++)
    {
        if (option_info[i].o_page != page) continue;

        if (option_info[i].o_var == &random_artifacts) continue;
        if (option_info[i].o_var == &reduce_uniques) continue;
        if (option_info[i].o_var == &ironman_empty_levels) continue;
        if (option_info[i].o_var == &always_small_levels) continue;
        if (option_info[i].o_var == &single_pantheon) continue;
        if (option_info[i].o_var == &guaranteed_pantheon) continue;
        if (option_info[i].o_var == &obj_list_width) continue;
        if (option_info[i].o_var == &mon_list_width) continue;

        if (*option_info[i].o_var)
            auto_dump_printf("Y:%s\n", option_info[i].o_text);
        else
            auto_dump_printf("X:%s\n", option_info[i].o_text);
    }

    switch (page)
    {
        case OPT_PAGE_MAPSCREEN:
            auto_dump_printf("O:delay_factor:%d\n", delay_factor);
            break;
        case OPT_PAGE_TEXT:
            auto_dump_printf("O:mana_warn:%d\n", mana_warn);
            break;
        case OPT_PAGE_GAMEPLAY:
            auto_dump_printf("O:autosave_l:%d\n", autosave_l ? 1 : 0);
            auto_dump_printf("O:autosave_t:%d\n", autosave_t ? 1 : 0);
            auto_dump_printf("O:autosave_freq:%d\n", autosave_freq);
            break;
        case OPT_PAGE_DISTURBANCE:
            auto_dump_printf("O:hitpoint_warn:%d\n", hitpoint_warn);
            break;
        case OPT_PAGE_LIST:
            auto_dump_printf("O:object_list_width:%d\n", object_list_width);
            auto_dump_printf("O:monster_list_width:%d\n", monster_list_width);
            break;
        default:
            break;
    }
}

static void config_dump_options_page_slot(int page, int slot, cptr desc, cptr name_root)
{
    char section[32];

    config_options_section(page, section, sizeof(section));
    if (!config_open_dump_slot(section, slot, desc, name_root)) return;

    config_dump_options_page(page);
    close_auto_dump();
}

static void config_options_save_page(int page)
{
    char section[32];
    config_slot_info_t slots[CONFIG_MAX_SLOTS];
    int slot;
    char desc[CONFIG_DESC_LEN];

    config_options_section(page, section, sizeof(section));
    config_scan_slots(section, slots);

    {
        config_options_preview_t preview = { page };
        if (!config_any_slots_used(slots, FALSE))
            slot = CONFIG_FIRST_USER_SLOT;
        else if (!config_prompt_slot_ex("Save Options (choose a slot)", slots, FALSE, FALSE, FALSE, FALSE,
            config_preview_options_page, &preview, &slot, NULL, NULL)) return;
    }

    if (slot >= CONFIG_FIRST_ALL_SLOT && !get_check("This modifies an All Settings profile. Continue? "))
        return;

    if (slots[slot].used && !get_check("Overwrite existing settings in this slot? "))
        return;

    if (!config_prompt_description(desc, sizeof(desc))) return;

    if (!config_open_dump(section, slot, desc)) return;

    config_dump_options_page(page);
    close_auto_dump();
    msg_print("Saved settings.");
}

static void config_options_save_all(void)
{
    config_slot_info_t slots[CONFIG_MAX_SLOTS];
    int slot;
    char desc[CONFIG_DESC_LEN];

    config_options_scan_all_slots(slots);
    if (!config_prompt_slot_ex("Save All Settings (choose a slot)", slots, TRUE, FALSE, FALSE, FALSE,
        config_preview_options_all, NULL, &slot, NULL, NULL)) return;

    if (slots[slot].used && !get_check("Overwrite existing All Settings profile? "))
        return;

    if (!config_prompt_description(desc, sizeof(desc))) return;

    config_dump_all_settings_slot(slot, desc, NULL);

    msg_print("Saved all settings.");
}

static void config_options_load_page(int page)
{
    char section[32];
    config_slot_info_t slots[CONFIG_MAX_SLOTS];
    int slot;
    options_snapshot_t cur;
    options_snapshot_t next;
    char diff_lines[512][160];
    int diff_count = 0;
    char choice;
    bool allow_all = FALSE;

    config_options_section(page, section, sizeof(section));
    config_scan_slots(section, slots);

    {
        config_options_preview_t preview = { page };
        if (!config_prompt_slot_ex("Load Options (choose a slot)", slots, FALSE, TRUE, FALSE, FALSE,
            config_preview_options_page, &preview, &slot, NULL, NULL)) return;
    }

    options_snapshot_current(&cur);
    next = cur;
    config_for_each_line(section, slot, config_apply_snapshot_line, &next);

    options_diff_page(page, &cur, &next, diff_lines, &diff_count, 512);
    allow_all = (slot >= CONFIG_FIRST_ALL_SLOT);
    choice = config_prompt_diff("Option Changes", diff_lines, diff_count, allow_all, TRUE);

    if (choice == 'a')
    {
        config_options_load_all_slot(slot);
        return;
    }
    if (choice != 'y') return;

    if (slot >= CONFIG_FIRST_ALL_SLOT)
        msg_print("Note: This loads only this options page. Use the main options menu to load all settings.");

    config_for_each_line(section, slot, config_apply_pref_line, NULL);
    msg_print("Loaded settings.");
}

static void config_options_load_all(void)
{
    config_slot_info_t slots[CONFIG_MAX_SLOTS];
    int slot;
    bool do_delete = FALSE;
    bool do_duplicate = FALSE;

    config_options_scan_all_slots(slots);
    if (!config_prompt_slot_ex("Load All Settings (choose a slot)", slots, TRUE, TRUE, TRUE, TRUE,
        config_preview_options_all, NULL, &slot, &do_delete, &do_duplicate)) return;
    if (do_delete)
    {
        if (!get_check("Delete this All Settings profile? ")) return;
        config_remove_all_settings_slot(slot);
        msg_print("Deleted all settings profile.");
        return;
    }
    if (do_duplicate)
    {
        int dst_slot;
        char desc[CONFIG_DESC_LEN];
        if (!config_prompt_slot_ex("Duplicate to which slot", slots, TRUE, FALSE, FALSE, FALSE,
            config_preview_options_all, NULL, &dst_slot, NULL, NULL)) return;

        if (dst_slot == slot)
        {
            msg_print("Source and destination are the same.");
            return;
        }
        if (slots[dst_slot].used && !get_check("Overwrite existing profile in destination slot? "))
            return;

        if (!config_prompt_description_with_default(desc, sizeof(desc), slots[slot].desc)) return;

        config_copy_all_settings_slot(slot, dst_slot, desc);
        msg_print("Duplicated all settings profile.");
        return;
    }
    config_options_load_all_slot(slot);
}

static void config_options_load_all_slot(int slot)
{
    options_snapshot_t cur;
    options_snapshot_t next;
    char diff_lines[512][160];
    int diff_count = 0;
    int page;
    char choice;
    int mode = rogue_like_commands ? KEYMAP_MODE_ROGUE : KEYMAP_MODE_ORIG;

    options_snapshot_current(&cur);
    next = cur;

    for (page = 1; page <= 7; page++)
    {
        char section[32];
        config_options_section(page, section, sizeof(section));
        config_for_each_line(section, slot, config_apply_snapshot_line, &next);
    }

    for (page = 1; page <= 7; page++)
        options_diff_page(page, &cur, &next, diff_lines, &diff_count, 512);

    choice = config_prompt_diff("All Settings Changes", diff_lines, diff_count, FALSE, TRUE);
    if (choice != 'y') return;

    for (page = 1; page <= 7; page++)
    {
        char section[32];
        config_options_section(page, section, sizeof(section));
        config_for_each_line(section, slot, config_apply_pref_line, NULL);
    }
    config_apply_window_flags_slot(slot);
    config_apply_macros_slot(slot);
    config_apply_keymaps_slot(slot, mode);
    config_apply_visuals_slot(slot);
    config_apply_birth_options_slot(slot);
    msg_print("Loaded all settings.");
}

static void config_options_reset_page(int page)
{
    options_snapshot_t cur;
    options_snapshot_t next;
    char diff_lines[512][160];
    int diff_count = 0;
    char choice;

    if (page == OPT_PAGE_BIRTH)
    {
        msg_print("Birth settings are reset from the birth screen.");
        return;
    }

    options_snapshot_current(&cur);
    options_snapshot_defaults(&next);
    options_diff_page(page, &cur, &next, diff_lines, &diff_count, 512);

    choice = config_prompt_diff("Reset Options", diff_lines, diff_count, TRUE, TRUE);
    if (choice == 'a')
    {
        config_options_reset_all();
        return;
    }
    if (choice != 'y') return;

    for (int i = 0; option_info[i].o_desc; i++)
    {
        if (option_info[i].o_page != page) continue;
        if (option_info[i].o_var)
            *option_info[i].o_var = option_info[i].o_norm ? TRUE : FALSE;
    }

    switch (page)
    {
        case OPT_PAGE_MAPSCREEN:
            delay_factor = DEFAULT_DELAY_FACTOR;
            break;
        case OPT_PAGE_TEXT:
            mana_warn = DEFAULT_MANA_WARN;
            break;
        case OPT_PAGE_GAMEPLAY:
            autosave_l = DEFAULT_AUTOSAVE_L;
            autosave_t = DEFAULT_AUTOSAVE_T;
            autosave_freq = DEFAULT_AUTOSAVE_FREQ;
            break;
        case OPT_PAGE_DISTURBANCE:
            hitpoint_warn = DEFAULT_HITPOINT_WARN;
            break;
        case OPT_PAGE_LIST:
            object_list_width = DEFAULT_OBJECT_LIST_WIDTH;
            monster_list_width = DEFAULT_MONSTER_LIST_WIDTH;
            break;
        default:
            break;
    }

    msg_print("Reset settings to defaults.");
}

static void config_options_reset_all(void)
{
    options_snapshot_t cur;
    options_snapshot_t next;
    char diff_lines[512][160];
    int diff_count = 0;
    int page;
    char choice;

    options_snapshot_current(&cur);
    options_snapshot_defaults(&next);
    for (page = 1; page <= 7; page++)
        options_diff_page(page, &cur, &next, diff_lines, &diff_count, 512);

    choice = config_prompt_diff("Reset All Settings", diff_lines, diff_count, FALSE, TRUE);
    if (choice != 'y') return;

    for (int i = 0; option_info[i].o_desc; i++)
    {
        if (!option_info[i].o_var) continue;
        if (option_info[i].o_page >= OPT_PAGE_INPUT && option_info[i].o_page <= OPT_PAGE_LIST)
            *option_info[i].o_var = option_info[i].o_norm ? TRUE : FALSE;
    }

    delay_factor = DEFAULT_DELAY_FACTOR;
    hitpoint_warn = DEFAULT_HITPOINT_WARN;
    mana_warn = DEFAULT_MANA_WARN;
    autosave_l = DEFAULT_AUTOSAVE_L;
    autosave_t = DEFAULT_AUTOSAVE_T;
    autosave_freq = DEFAULT_AUTOSAVE_FREQ;
    object_list_width = DEFAULT_OBJECT_LIST_WIDTH;
    monster_list_width = DEFAULT_MONSTER_LIST_WIDTH;

    msg_print("Reset all settings to defaults.");
}

static void config_macros_section(char *buf, size_t buf_len)
{
    strnfmt(buf, buf_len, "macros");
}

static void config_keymaps_section(int mode, char *buf, size_t buf_len)
{
    if (mode == KEYMAP_MODE_ROGUE)
        strnfmt(buf, buf_len, "keymaps-rogue");
    else
        strnfmt(buf, buf_len, "keymaps-orig");
}

static void config_visuals_section(char *buf, size_t buf_len)
{
    strnfmt(buf, buf_len, "visuals");
}

static void config_window_flags_section(char *buf, size_t buf_len)
{
    strnfmt(buf, buf_len, "window-flags");
}

static void config_dump_macros_slot(int slot, cptr desc, cptr name_root)
{
    char section[32];
    int i;
    char buf[1024];

    config_macros_section(section, sizeof(section));
    if (!config_open_dump_slot(section, slot, desc, name_root)) return;

    auto_dump_printf("# Macro definitions\n");
    for (i = 0; i < macro__num; i++)
    {
        if (streq(macro__pat[i], macro__act[i])) continue;
        ascii_to_text(buf, macro__act[i]);
        auto_dump_printf("A:%s\n", buf);
        ascii_to_text(buf, macro__pat[i]);
        auto_dump_printf("P:%s\n\n", buf);
    }

    close_auto_dump();
}

static void config_dump_keymaps_slot(int slot, int mode, cptr desc, cptr name_root)
{
    char section[32];
    int i;
    char buf[1024];
    char key[32];

    config_keymaps_section(mode, section, sizeof(section));
    if (!config_open_dump_slot(section, slot, desc, name_root)) return;

    auto_dump_printf("# Keymap definitions\n");
    for (i = 0; i < 256; i++)
    {
        cptr act = keymap_act[mode][i];
        if (!act) continue;
        ascii_to_text(buf, act);
        auto_dump_printf("A:%s\n", buf);
        buf[0] = (char)i;
        buf[1] = '\0';
        ascii_to_text(key, buf);
        auto_dump_printf("C:%d:%s\n", mode, key);
    }

    close_auto_dump();
}

static void config_dump_visuals_slot(int slot, cptr desc, cptr name_root)
{
    char section[32];
    int i;

    config_visuals_section(section, sizeof(section));
    if (!config_open_dump_slot(section, slot, desc, name_root)) return;

    auto_dump_printf("# Visual definitions\n");

    for (i = 0; i < max_r_idx; i++)
    {
        monster_race *r_ptr = &r_info[i];
        if (!r_ptr->name) continue;
        if (r_ptr->x_attr == r_ptr->d_attr && r_ptr->x_char == r_ptr->d_char) continue;
        auto_dump_printf("# %s\n", (r_name + r_ptr->name));
        auto_dump_printf("R:%d:0x%02X/0x%02X\n\n", i, r_ptr->x_attr, r_ptr->x_char);
    }

    for (i = 0; i < max_k_idx; i++)
    {
        object_kind *k_ptr = &k_info[i];
        char o_name[80];
        if (!k_ptr->name) continue;
        if (k_ptr->x_attr == k_ptr->d_attr && k_ptr->x_char == k_ptr->d_char) continue;
        if (!k_ptr->flavor) strip_name(o_name, i);
        else
        {
            object_type forge;
            object_prep(&forge, i);
            object_desc(o_name, &forge, OD_FORCE_FLAVOR);
        }
        auto_dump_printf("# %s\n", o_name);
        auto_dump_printf("K:%d:%d:0x%02X/0x%02X\n\n",
            k_ptr->tval, k_ptr->sval, k_ptr->x_attr, k_ptr->x_char);
    }

    for (i = 0; i < max_f_idx; i++)
    {
        feature_type *f_ptr = &f_info[i];
        if (!f_ptr->name) continue;
        if (f_ptr->mimic != i) continue;
        if (f_ptr->x_attr[F_LIT_STANDARD] == f_ptr->d_attr[F_LIT_STANDARD] &&
            f_ptr->x_char[F_LIT_STANDARD] == f_ptr->d_char[F_LIT_STANDARD] &&
            f_ptr->x_attr[F_LIT_LITE] == f_ptr->d_attr[F_LIT_LITE] &&
            f_ptr->x_char[F_LIT_LITE] == f_ptr->d_char[F_LIT_LITE] &&
            f_ptr->x_attr[F_LIT_DARK] == f_ptr->d_attr[F_LIT_DARK] &&
            f_ptr->x_char[F_LIT_DARK] == f_ptr->d_char[F_LIT_DARK])
            continue;

        auto_dump_printf("# %s\n", (f_name + f_ptr->name));
        auto_dump_printf("F:%d:0x%02X/0x%02X:0x%02X/0x%02X:0x%02X/0x%02X\n\n", i,
            f_ptr->x_attr[F_LIT_STANDARD], f_ptr->x_char[F_LIT_STANDARD],
            f_ptr->x_attr[F_LIT_LITE], f_ptr->x_char[F_LIT_LITE],
            f_ptr->x_attr[F_LIT_DARK], f_ptr->x_char[F_LIT_DARK]);
    }

    close_auto_dump();
}

static void config_dump_window_flags_slot(int slot, cptr desc, cptr name_root)
{
    char section[32];
    int term;

    config_window_flags_section(section, sizeof(section));
    if (!config_open_dump_slot(section, slot, desc, name_root)) return;

    auto_dump_printf("# Window flag order and locks\n");

    for (term = 1; term < 8; term++)
    {
        int i;

        if (window_flag_order_count[term] == 0) continue;

        auto_dump_printf("W:%d:%d:", term, window_flag_order_index[term]);
        for (i = 0; i < window_flag_order_count[term]; i++)
        {
            if (i) auto_dump_printf(",");
            auto_dump_printf("%d", window_flag_order[term][i]);
        }
        auto_dump_printf("\n");
    }

    close_auto_dump();
}

static void config_dump_birth_slot(int slot, cptr desc, cptr name_root)
{
    if (!config_open_dump_slot("birth", slot, desc, name_root)) return;

    auto_dump_printf("# Birth profile\n");
    auto_dump_printf("B:game_mode:%d\n", game_mode);
    auto_dump_printf("B:psex:%d\n", p_ptr->psex);
    auto_dump_printf("B:prace:%d\n", p_ptr->prace);
    auto_dump_printf("B:psubrace:%d\n", p_ptr->psubrace);
    auto_dump_printf("B:pclass:%d\n", p_ptr->pclass);
    auto_dump_printf("B:psubclass:%d\n", p_ptr->psubclass);
    auto_dump_printf("B:personality:%d\n", p_ptr->personality);
    auto_dump_printf("B:realm1:%d\n", p_ptr->realm1);
    auto_dump_printf("B:realm2:%d\n", p_ptr->realm2);
    auto_dump_printf("B:dragon_realm:%d\n", p_ptr->dragon_realm);

    config_dump_options_page(OPT_PAGE_BIRTH);
    close_auto_dump();
}

static void config_dump_all_settings_slot(int slot, cptr desc, cptr name_root)
{
    int mode = rogue_like_commands ? KEYMAP_MODE_ROGUE : KEYMAP_MODE_ORIG;
    int page;

    for (page = 1; page <= 7; page++)
        config_dump_options_page_slot(page, slot, desc, name_root);
    config_dump_window_flags_slot(slot, desc, name_root);
    config_dump_macros_slot(slot, desc, name_root);
    config_dump_keymaps_slot(slot, mode, desc, name_root);
    config_dump_visuals_slot(slot, desc, name_root);
    config_dump_birth_slot(slot, desc, name_root);
}

static void config_apply_macros_slot(int slot)
{
    char section[32];
    config_macros_section(section, sizeof(section));
    macro_clear_all();
    config_for_each_line(section, slot, config_apply_pref_line, NULL);
}

static void config_apply_keymaps_slot(int slot, int mode)
{
    char section[32];
    config_keymaps_section(mode, section, sizeof(section));
    keymap_clear_mode(mode);
    config_for_each_line(section, slot, config_apply_pref_line, NULL);
}

static void config_apply_visuals_slot(int slot)
{
    char section[32];
    config_visuals_section(section, sizeof(section));
    config_for_each_line(section, slot, config_apply_pref_line, NULL);
}

static void config_apply_window_flags_slot(int slot)
{
    char section[32];
    int term;
    u32b mask = 0L;
    config_window_flags_section(section, sizeof(section));
    config_for_each_line(section, slot, config_apply_pref_line, NULL);
    window_flag_order_sync_all();
    for (term = 1; term < 8; term++)
        mask |= window_flag_active[term];
    if (mask)
    {
        p_ptr->window |= mask;
        window_stuff();
    }
}

static void config_apply_birth_options_slot(int slot)
{
    config_for_each_line("birth", slot, config_apply_pref_line_no_birth, NULL);
}

static void config_remove_all_settings_slot(int slot)
{
    int page;
    for (page = 1; page <= 7; page++)
    {
        char section[32];
        config_options_section(page, section, sizeof(section));
        config_remove_block(section, slot);
    }
    config_remove_block("macros", slot);
    config_remove_block("keymaps-rogue", slot);
    config_remove_block("keymaps-orig", slot);
    config_remove_block("visuals", slot);
    config_remove_block("window-flags", slot);
    config_remove_block("birth", slot);
}

static void config_copy_all_settings_slot(int src_slot, int dst_slot, cptr desc)
{
    int page;
    for (page = 1; page <= 7; page++)
    {
        char section[32];
        config_options_section(page, section, sizeof(section));
        config_copy_block(section, src_slot, dst_slot, desc);
    }
    config_copy_block("macros", src_slot, dst_slot, desc);
    config_copy_block("keymaps-rogue", src_slot, dst_slot, desc);
    config_copy_block("keymaps-orig", src_slot, dst_slot, desc);
    config_copy_block("visuals", src_slot, dst_slot, desc);
    config_copy_block("window-flags", src_slot, dst_slot, desc);
    config_copy_block("birth", src_slot, dst_slot, desc);
}

typedef struct config_keymap_preview_s config_keymap_preview_t;
struct config_keymap_preview_s
{
    int mode;
};

static void config_scan_slots_macros_keymaps(config_slot_info_t slots[CONFIG_MAX_SLOTS], int mode)
{
    config_slot_info_t macro_slots[CONFIG_MAX_SLOTS];
    config_slot_info_t keymap_slots[CONFIG_MAX_SLOTS];
    char section[32];
    int i;

    config_scan_slots("macros", macro_slots);
    config_keymaps_section(mode, section, sizeof(section));
    config_scan_slots(section, keymap_slots);

    for (i = 0; i < CONFIG_MAX_SLOTS; i++)
    {
        bool macro_used = macro_slots[i].used;
        bool keymap_used = keymap_slots[i].used;
        slots[i].used = macro_used || keymap_used;
        slots[i].desc[0] = '\0';
        if (macro_used && keymap_used)
        {
            if (streq(macro_slots[i].desc, keymap_slots[i].desc))
                strnfmt(slots[i].desc, sizeof(slots[i].desc), "%s", macro_slots[i].desc);
            else
                strnfmt(slots[i].desc, sizeof(slots[i].desc), "Macros: %s / Keymaps: %s",
                    macro_slots[i].desc, keymap_slots[i].desc);
        }
        else if (macro_used)
            strnfmt(slots[i].desc, sizeof(slots[i].desc), "%s", macro_slots[i].desc);
        else if (keymap_used)
            strnfmt(slots[i].desc, sizeof(slots[i].desc), "%s", keymap_slots[i].desc);
    }
}

static void config_preview_macros_keymaps(int slot, void *data)
{
    config_keymap_preview_t *preview = (config_keymap_preview_t *)data;
    config_slot_info_t macro_slots[CONFIG_MAX_SLOTS];
    config_slot_info_t keymap_slots[CONFIG_MAX_SLOTS];
    char section[32];
    bool any = FALSE;

    config_scan_slots("macros", macro_slots);
    config_keymaps_section(preview->mode, section, sizeof(section));
    config_scan_slots(section, keymap_slots);

    if (macro_slots[slot].used)
    {
        any = TRUE;
        config_preview_macros(slot, NULL);
    }

    if (keymap_slots[slot].used)
    {
        any = TRUE;
        config_preview_keymaps(slot, preview);
    }

    if (!any)
        msg_print("Slot is empty.");
}

static void config_preview_macros(int slot, void *data)
{
    char section[32];
    config_slot_info_t slots[CONFIG_MAX_SLOTS];
    macro_snapshot_t cur;
    macro_snapshot_t next;
    config_diff_entry_t diff_entries[512];
    int diff_count = 0;

    (void)data;
    config_macros_section(section, sizeof(section));
    config_scan_slots(section, slots);
    if (!slots[slot].used)
    {
        msg_print("Slot is empty.");
        return;
    }

    macro_snapshot_current(&cur);
    macro_snapshot_from_file(&next, section, slot);
    config_macro_diff(&cur, &next, diff_entries, &diff_count, 512);
    config_prompt_diff_list("Macro Preview", diff_entries, diff_count, FALSE);
    macro_snapshot_free(&cur);
    macro_snapshot_free(&next);
}

static void config_preview_keymaps(int slot, void *data)
{
    config_keymap_preview_t *preview = (config_keymap_preview_t *)data;
    char section[32];
    config_slot_info_t slots[CONFIG_MAX_SLOTS];
    keymap_snapshot_t cur;
    keymap_snapshot_t next;
    config_diff_entry_t diff_entries[512];
    int diff_count = 0;

    config_keymaps_section(preview->mode, section, sizeof(section));
    config_scan_slots(section, slots);
    if (!slots[slot].used)
    {
        msg_print("Slot is empty.");
        return;
    }

    keymap_snapshot_current(&cur, preview->mode);
    memset(&next, 0, sizeof(next));
    keymap_snapshot_from_file(&next, section, slot, preview->mode);
    config_keymap_diff(&cur, &next, diff_entries, &diff_count, 512);
    config_prompt_diff_list("Keymap Preview", diff_entries, diff_count, FALSE);
    keymap_snapshot_free(&cur);
    keymap_snapshot_free(&next);
}

static void config_preview_visuals(int slot, void *data)
{
    char section[32];
    config_slot_info_t slots[CONFIG_MAX_SLOTS];
    config_diff_entry_t diff_entries[512];
    int diff_count = 0;
    visuals_diff_state_t state;

    (void)data;
    config_visuals_section(section, sizeof(section));
    config_scan_slots(section, slots);
    if (!slots[slot].used)
    {
        msg_print("Slot is empty.");
        return;
    }

    state.entries = diff_entries;
    state.count = &diff_count;
    state.max = 512;
    config_for_each_line(section, slot, visuals_diff_apply_line, &state);
    config_prompt_diff_list("Visual Preview", diff_entries, diff_count, FALSE);
}

static void config_macro_diff(const macro_snapshot_t *cur, const macro_snapshot_t *next,
    config_diff_entry_t *entries, int *count, int max)
{
    int i;
    for (i = 0; i < cur->count; i++)
    {
        int idx = macro_snapshot_find(next, cur->pat[i]);
        if (idx < 0 || !streq(cur->act[i], next->act[idx]))
        {
            char pat_txt[256];
            char old_act[256];
            char new_act[256];
            if (*count >= max) break;
            ascii_to_text(pat_txt, cur->pat[i]);
            ascii_to_text(old_act, cur->act[i]);
            if (idx >= 0) ascii_to_text(new_act, next->act[idx]);
            else strnfmt(new_act, sizeof(new_act), "<none>");
            strnfmt(entries[*count].label, sizeof(entries[*count].label), "%s", pat_txt);
            strnfmt(entries[*count].old_val, sizeof(entries[*count].old_val), "%s", old_act);
            strnfmt(entries[*count].new_val, sizeof(entries[*count].new_val), "%s", new_act);
            strnfmt(entries[*count].detail1, sizeof(entries[*count].detail1), "Old: %s", old_act);
            strnfmt(entries[*count].detail2, sizeof(entries[*count].detail2), "New: %s", new_act);
            (*count)++;
        }
    }

    for (i = 0; i < next->count; i++)
    {
        if (macro_snapshot_find(cur, next->pat[i]) >= 0) continue;
        if (*count >= max) break;
        {
            char pat_txt[256];
            char new_act[256];
            ascii_to_text(pat_txt, next->pat[i]);
            ascii_to_text(new_act, next->act[i]);
            strnfmt(entries[*count].label, sizeof(entries[*count].label), "%s", pat_txt);
            strnfmt(entries[*count].old_val, sizeof(entries[*count].old_val), "<none>");
            strnfmt(entries[*count].new_val, sizeof(entries[*count].new_val), "%s", new_act);
            strnfmt(entries[*count].detail1, sizeof(entries[*count].detail1), "Old: <none>");
            strnfmt(entries[*count].detail2, sizeof(entries[*count].detail2), "New: %s", new_act);
            (*count)++;
        }
    }
}

static void config_keymap_diff(const keymap_snapshot_t *cur, const keymap_snapshot_t *next,
    config_diff_entry_t *entries, int *count, int max)
{
    int i;
    char key_txt[128];
    char old_act[256];
    char new_act[256];
    char key_buf[2];

    for (i = 0; i < 256; i++)
    {
        cptr old_ptr = cur->act[i];
        cptr new_ptr = next->act[i];
        if (old_ptr == new_ptr) continue;
        if (old_ptr && new_ptr && streq(old_ptr, new_ptr)) continue;
        if (*count >= max) break;
        key_buf[0] = (char)i;
        key_buf[1] = '\0';
        ascii_to_text(key_txt, key_buf);
        if (old_ptr) ascii_to_text(old_act, old_ptr);
        else strnfmt(old_act, sizeof(old_act), "<none>");
        if (new_ptr) ascii_to_text(new_act, new_ptr);
        else strnfmt(new_act, sizeof(new_act), "<none>");
        strnfmt(entries[*count].label, sizeof(entries[*count].label), "%s", key_txt);
        strnfmt(entries[*count].old_val, sizeof(entries[*count].old_val), "%s", old_act);
        strnfmt(entries[*count].new_val, sizeof(entries[*count].new_val), "%s", new_act);
        strnfmt(entries[*count].detail1, sizeof(entries[*count].detail1), "Old: %s", old_act);
        strnfmt(entries[*count].detail2, sizeof(entries[*count].detail2), "New: %s", new_act);
        (*count)++;
    }
}

static void config_diff_prefix(config_diff_entry_t *entries, int start, int count, cptr prefix)
{
    int i;
    for (i = start; i < start + count; i++)
    {
        char buf[80];
        strnfmt(buf, sizeof(buf), "%s%s", prefix, entries[i].label);
        strnfmt(entries[i].label, sizeof(entries[i].label), "%s", buf);
    }
}

typedef struct visuals_diff_state_s visuals_diff_state_t;
struct visuals_diff_state_s
{
    config_diff_entry_t *entries;
    int *count;
    int max;
};

static void visuals_diff_add(visuals_diff_state_t *state, cptr label, cptr old_val, cptr new_val, cptr detail1, cptr detail2)
{
    if (*state->count >= state->max) return;
    strnfmt(state->entries[*state->count].label, sizeof(state->entries[*state->count].label), "%s", label);
    strnfmt(state->entries[*state->count].old_val, sizeof(state->entries[*state->count].old_val), "%s", old_val);
    strnfmt(state->entries[*state->count].new_val, sizeof(state->entries[*state->count].new_val), "%s", new_val);
    strnfmt(state->entries[*state->count].detail1, sizeof(state->entries[*state->count].detail1), "%s", detail1);
    strnfmt(state->entries[*state->count].detail2, sizeof(state->entries[*state->count].detail2), "%s", detail2);
    (*state->count)++;
}

static int visuals_diff_apply_line(cptr line, void *data)
{
    visuals_diff_state_t *state = (visuals_diff_state_t *)data;
    char buf[1024];
    char *zz[5];

    strnfmt(buf, sizeof(buf), "%s", line);
    if (buf[1] != ':') return 0;

    if (buf[0] == 'R' && tokenize(buf + 2, 2, zz, TOKENIZE_CHECKQUOTE) == 2)
    {
        int idx = strtol(zz[0], NULL, 0);
        unsigned int attr = strtol(zz[1], NULL, 0);
        unsigned int chr = 0;
        if (strchr(zz[1], '/'))
        {
            sscanf(zz[1], "0x%X/0x%X", &attr, &chr);
        }
        else
        {
            sscanf(zz[1], "%u/%u", &attr, &chr);
        }
        if (idx > 0 && idx < max_r_idx && r_info[idx].name)
        {
            monster_race *r_ptr = &r_info[idx];
            if (r_ptr->x_attr != (byte)attr || r_ptr->x_char != (byte)chr)
            {
                char label[160];
                char old_val[64];
                char new_val[64];
                char detail1[128];
                char detail2[128];
                strnfmt(label, sizeof(label), "Monster: %s", (r_name + r_ptr->name));
                strnfmt(old_val, sizeof(old_val), "0x%02X/0x%02X", r_ptr->x_attr, r_ptr->x_char);
                strnfmt(new_val, sizeof(new_val), "0x%02X/0x%02X", (byte)attr, (byte)chr);
                strnfmt(detail1, sizeof(detail1), "Old: %s", old_val);
                strnfmt(detail2, sizeof(detail2), "New: %s", new_val);
                visuals_diff_add(state, label, old_val, new_val, detail1, detail2);
            }
        }
        return 0;
    }

    if (buf[0] == 'K' && tokenize(buf + 2, 3, zz, TOKENIZE_CHECKQUOTE) == 3)
    {
        int tval = strtol(zz[0], NULL, 0);
        int sval = strtol(zz[1], NULL, 0);
        unsigned int attr = 0;
        unsigned int chr = 0;
        sscanf(zz[2], "0x%X/0x%X", &attr, &chr);
        {
            int i;
            for (i = 0; i < max_k_idx; i++)
            {
                object_kind *k_ptr = &k_info[i];
                if (!k_ptr->name) continue;
                if (k_ptr->tval != tval || k_ptr->sval != sval) continue;
                if (k_ptr->x_attr != (byte)attr || k_ptr->x_char != (byte)chr)
                {
                    char label[160];
                    char old_val[64];
                    char new_val[64];
                    char detail1[128];
                    char detail2[128];
                    char o_name[80];
                    if (!k_ptr->flavor)
                        strip_name(o_name, i);
                    else
                    {
                        object_type forge;
                        object_prep(&forge, i);
                        object_desc(o_name, &forge, OD_FORCE_FLAVOR);
                    }
                    strnfmt(label, sizeof(label), "Object: %s", o_name);
                    strnfmt(old_val, sizeof(old_val), "0x%02X/0x%02X", k_ptr->x_attr, k_ptr->x_char);
                    strnfmt(new_val, sizeof(new_val), "0x%02X/0x%02X", (byte)attr, (byte)chr);
                    strnfmt(detail1, sizeof(detail1), "Old: %s", old_val);
                    strnfmt(detail2, sizeof(detail2), "New: %s", new_val);
                    visuals_diff_add(state, label, old_val, new_val, detail1, detail2);
                }
                break;
            }
        }
        return 0;
    }

    if (buf[0] == 'F' && tokenize(buf + 2, 2, zz, TOKENIZE_CHECKQUOTE) == 2)
    {
        int idx = strtol(zz[0], NULL, 0);
        unsigned int a1, c1, a2, c2, a3, c3;
        if (sscanf(zz[1], "0x%X/0x%X:0x%X/0x%X:0x%X/0x%X", &a1, &c1, &a2, &c2, &a3, &c3) == 6)
        {
            if (idx >= 0 && idx < max_f_idx && f_info[idx].name)
            {
                feature_type *f_ptr = &f_info[idx];
                if (f_ptr->x_attr[F_LIT_STANDARD] != (byte)a1 || f_ptr->x_char[F_LIT_STANDARD] != (byte)c1 ||
                    f_ptr->x_attr[F_LIT_LITE] != (byte)a2 || f_ptr->x_char[F_LIT_LITE] != (byte)c2 ||
                    f_ptr->x_attr[F_LIT_DARK] != (byte)a3 || f_ptr->x_char[F_LIT_DARK] != (byte)c3)
                {
                    char label[160];
                    char old_val[120];
                    char new_val[120];
                    char detail1[160];
                    char detail2[160];
                    strnfmt(label, sizeof(label), "Feature: %s", (f_name + f_ptr->name));
                    strnfmt(old_val, sizeof(old_val), "Std %02X/%02X Lit %02X/%02X Dark %02X/%02X",
                        f_ptr->x_attr[F_LIT_STANDARD], f_ptr->x_char[F_LIT_STANDARD],
                        f_ptr->x_attr[F_LIT_LITE], f_ptr->x_char[F_LIT_LITE],
                        f_ptr->x_attr[F_LIT_DARK], f_ptr->x_char[F_LIT_DARK]);
                    strnfmt(new_val, sizeof(new_val), "Std %02X/%02X Lit %02X/%02X Dark %02X/%02X",
                        (byte)a1, (byte)c1, (byte)a2, (byte)c2, (byte)a3, (byte)c3);
                    strnfmt(detail1, sizeof(detail1), "Old: %s", old_val);
                    strnfmt(detail2, sizeof(detail2), "New: %s", new_val);
                    visuals_diff_add(state, label, old_val, new_val, detail1, detail2);
                }
            }
        }
        return 0;
    }

    return 0;
}

static void config_macros_save(void)
{
    config_slot_info_t slots[CONFIG_MAX_SLOTS];
    int slot;
    char desc[CONFIG_DESC_LEN];
    int mode = rogue_like_commands ? KEYMAP_MODE_ROGUE : KEYMAP_MODE_ORIG;

    config_scan_slots_macros_keymaps(slots, mode);

    if (!config_any_slots_used(slots, FALSE))
        slot = CONFIG_FIRST_USER_SLOT;
    else
    {
        config_keymap_preview_t preview = { mode };
        if (!config_prompt_slot_ex("Save Macros/Keymaps (choose a slot)", slots, FALSE, FALSE, FALSE, FALSE,
            config_preview_macros_keymaps, &preview, &slot, NULL, NULL)) return;
    }

    if (slot >= CONFIG_FIRST_ALL_SLOT && !get_check("This modifies an All Settings profile. Continue? "))
        return;

    if (slots[slot].used && !get_check("Overwrite existing macros/keymaps in this slot? "))
        return;

    if (!config_prompt_description(desc, sizeof(desc))) return;
    config_dump_macros_slot(slot, desc, NULL);
    config_dump_keymaps_slot(slot, mode, desc, NULL);
    msg_print("Saved macros/keymaps.");
}

static void config_macros_load(void)
{
    config_slot_info_t slots[CONFIG_MAX_SLOTS];
    int slot;
    int mode = rogue_like_commands ? KEYMAP_MODE_ROGUE : KEYMAP_MODE_ORIG;
    macro_snapshot_t macro_cur;
    macro_snapshot_t macro_next;
    keymap_snapshot_t keymap_cur;
    keymap_snapshot_t keymap_next;
    config_diff_entry_t diff_entries[512];
    int diff_count = 0;
    int macro_count = 0;
    char section[32];

    config_scan_slots_macros_keymaps(slots, mode);
    {
        config_keymap_preview_t preview = { mode };
        if (!config_prompt_slot_ex("Load Macros/Keymaps (choose a slot)", slots, FALSE, TRUE, FALSE, FALSE,
            config_preview_macros_keymaps, &preview, &slot, NULL, NULL)) return;
    }

    macro_snapshot_current(&macro_cur);
    macro_snapshot_from_file(&macro_next, "macros", slot);
    config_macro_diff(&macro_cur, &macro_next, diff_entries, &diff_count, 512);
    macro_count = diff_count;
    if (macro_count > 0)
        config_diff_prefix(diff_entries, 0, macro_count, "Macro: ");

    keymap_snapshot_current(&keymap_cur, mode);
    memset(&keymap_next, 0, sizeof(keymap_next));
    config_keymaps_section(mode, section, sizeof(section));
    keymap_snapshot_from_file(&keymap_next, section, slot, mode);
    config_keymap_diff(&keymap_cur, &keymap_next, diff_entries, &diff_count, 512);
    if (diff_count > macro_count)
        config_diff_prefix(diff_entries, macro_count, diff_count - macro_count, "Keymap: ");

    if (!config_prompt_diff_list("Macro/Keymap Changes", diff_entries, diff_count, TRUE))
    {
        macro_snapshot_free(&macro_cur);
        macro_snapshot_free(&macro_next);
        keymap_snapshot_free(&keymap_cur);
        keymap_snapshot_free(&keymap_next);
        return;
    }

    if (slot >= CONFIG_FIRST_ALL_SLOT)
        msg_print("Note: This loads only macros/keymaps. Use the main options menu to load all settings.");

    macro_clear_all();
    keymap_clear_mode(mode);
    config_for_each_line("macros", slot, config_apply_pref_line, NULL);
    config_for_each_line(section, slot, config_apply_pref_line, NULL);

    macro_snapshot_free(&macro_cur);
    macro_snapshot_free(&macro_next);
    keymap_snapshot_free(&keymap_cur);
    keymap_snapshot_free(&keymap_next);
    msg_print("Loaded macros/keymaps.");
}

static void config_visuals_save(void)
{
    char section[32];
    config_slot_info_t slots[CONFIG_MAX_SLOTS];
    int slot;
    char desc[CONFIG_DESC_LEN];
    config_visuals_section(section, sizeof(section));
    config_scan_slots(section, slots);

    if (!config_any_slots_used(slots, FALSE))
        slot = CONFIG_FIRST_USER_SLOT;
    else if (!config_prompt_slot_ex("Save Visuals (choose a slot)", slots, FALSE, FALSE, FALSE, FALSE,
        config_preview_visuals, NULL, &slot, NULL, NULL)) return;

    if (slot >= CONFIG_FIRST_ALL_SLOT && !get_check("This modifies an All Settings profile. Continue? "))
        return;

    if (slots[slot].used && !get_check("Overwrite existing visuals in this slot? "))
        return;

    if (!config_prompt_description(desc, sizeof(desc))) return;
    config_dump_visuals_slot(slot, desc, NULL);
    msg_print("Saved visuals.");
}

static void config_visuals_load(void)
{
    char section[32];
    config_slot_info_t slots[CONFIG_MAX_SLOTS];
    int slot;
    config_diff_entry_t diff_entries[512];
    int diff_count = 0;
    visuals_diff_state_t state;

    config_visuals_section(section, sizeof(section));
    config_scan_slots(section, slots);
    if (!config_prompt_slot_ex("Load Visuals (choose a slot)", slots, FALSE, TRUE, FALSE, FALSE,
        config_preview_visuals, NULL, &slot, NULL, NULL)) return;

    state.entries = diff_entries;
    state.count = &diff_count;
    state.max = 512;

    config_for_each_line(section, slot, visuals_diff_apply_line, &state);

    if (!config_prompt_diff_list("Visual Changes", diff_entries, diff_count, TRUE))
        return;

    if (slot >= CONFIG_FIRST_ALL_SLOT)
        msg_print("Note: This loads only visuals. Use the main options menu to load all settings.");

    config_for_each_line(section, slot, config_apply_pref_line, NULL);
    msg_print("Loaded visuals.");
}

typedef struct birth_snapshot_s birth_snapshot_t;
struct birth_snapshot_s
{
    int game_mode;
    int psex;
    int prace;
    int psubrace;
    int pclass;
    int psubclass;
    int personality;
    int realm1;
    int realm2;
    int dragon_realm;
};

static cptr birth_game_mode_name(int mode)
{
    switch (mode)
    {
        case GAME_MODE_BEGINNER: return "Beginner";
        case GAME_MODE_MONSTER: return "Monster";
        default: return "Normal";
    }
}

static cptr birth_realm_name(int realm)
{
    if (realm <= 0) return "None";
    return realm_names[realm];
}

static cptr birth_race_name(int prace, int psubrace)
{
    race_t *race_ptr = get_race_aux(prace, psubrace);
    return race_ptr ? race_ptr->name : "Unknown";
}

static cptr birth_class_name(int pclass, int psubclass)
{
    class_t *class_ptr = get_class_aux(pclass, psubclass);
    return class_ptr ? class_ptr->name : "Unknown";
}

static cptr birth_personality_name(int personality)
{
    personality_ptr pers_ptr = get_personality_aux(personality);
    return pers_ptr ? pers_ptr->name : "Unknown";
}

static void birth_snapshot_current(birth_snapshot_t *snap)
{
    snap->game_mode = game_mode;
    snap->psex = p_ptr->psex;
    snap->prace = p_ptr->prace;
    snap->psubrace = p_ptr->psubrace;
    snap->pclass = p_ptr->pclass;
    snap->psubclass = p_ptr->psubclass;
    snap->personality = p_ptr->personality;
    snap->realm1 = p_ptr->realm1;
    snap->realm2 = p_ptr->realm2;
    snap->dragon_realm = p_ptr->dragon_realm;
}

typedef struct birth_parse_state_s birth_parse_state_t;
struct birth_parse_state_s
{
    birth_snapshot_t *snap;
};

static int birth_snapshot_apply_line(cptr line, void *data)
{
    birth_parse_state_t *state = (birth_parse_state_t *)data;
    char buf[1024];
    char *zz[3];
    int val;

    strnfmt(buf, sizeof(buf), "%s", line);
    if (buf[1] != ':') return 0;
    if (buf[0] != 'B') return 0;
    if (tokenize(buf + 2, 2, zz, TOKENIZE_CHECKQUOTE) != 2) return 0;
    val = strtol(zz[1], NULL, 0);

    if (streq(zz[0], "game_mode")) state->snap->game_mode = val;
    else if (streq(zz[0], "psex")) state->snap->psex = val;
    else if (streq(zz[0], "prace")) state->snap->prace = val;
    else if (streq(zz[0], "psubrace")) state->snap->psubrace = val;
    else if (streq(zz[0], "pclass")) state->snap->pclass = val;
    else if (streq(zz[0], "psubclass")) state->snap->psubclass = val;
    else if (streq(zz[0], "personality")) state->snap->personality = val;
    else if (streq(zz[0], "realm1")) state->snap->realm1 = val;
    else if (streq(zz[0], "realm2")) state->snap->realm2 = val;
    else if (streq(zz[0], "dragon_realm")) state->snap->dragon_realm = val;

    return 0;
}

static void birth_snapshot_from_file(birth_snapshot_t *snap, cptr section, int slot)
{
    birth_parse_state_t state;
    birth_snapshot_current(snap);
    state.snap = snap;
    config_for_each_line(section, slot, birth_snapshot_apply_line, &state);
}

static void birth_diff_add(config_diff_entry_t *entries, int *count, int max, cptr label, cptr old_val, cptr new_val)
{
    if (*count >= max) return;
    strnfmt(entries[*count].label, sizeof(entries[*count].label), "%s", label);
    strnfmt(entries[*count].old_val, sizeof(entries[*count].old_val), "%s", old_val);
    strnfmt(entries[*count].new_val, sizeof(entries[*count].new_val), "%s", new_val);
    strnfmt(entries[*count].detail1, sizeof(entries[*count].detail1), "Old: %s", old_val);
    strnfmt(entries[*count].detail2, sizeof(entries[*count].detail2), "New: %s", new_val);
    (*count)++;
}

static bool config_birth_build_diff(int slot, config_diff_entry_t *diff_entries, int *diff_count)
{
    char section[32];
    config_slot_info_t slots[CONFIG_MAX_SLOTS];
    birth_snapshot_t cur;
    birth_snapshot_t next;
    options_snapshot_t cur_opts;
    options_snapshot_t next_opts;
    char diff_lines[256][160];
    int diff_line_count = 0;
    int i;

    strnfmt(section, sizeof(section), "birth");
    config_scan_slots(section, slots);
    if (!slots[slot].used)
        return FALSE;

    birth_snapshot_current(&cur);
    birth_snapshot_from_file(&next, section, slot);


    if (cur.game_mode != next.game_mode)
        birth_diff_add(diff_entries, diff_count, 512, "Game Mode",
            birth_game_mode_name(cur.game_mode), birth_game_mode_name(next.game_mode));
    if (cur.psex != next.psex)
        birth_diff_add(diff_entries, diff_count, 512, "Sex",
            sex_info[cur.psex].title, sex_info[next.psex].title);

    if (cur.prace != next.prace || cur.psubrace != next.psubrace)
    {
        cptr old_race = birth_race_name(cur.prace, cur.psubrace);
        cptr new_race = birth_race_name(next.prace, next.psubrace);
        birth_diff_add(diff_entries, diff_count, 512, "Race", old_race, new_race);
    }

    if (cur.pclass != next.pclass || cur.psubclass != next.psubclass)
    {
        cptr old_class = birth_class_name(cur.pclass, cur.psubclass);
        cptr new_class = birth_class_name(next.pclass, next.psubclass);
        birth_diff_add(diff_entries, diff_count, 512, "Class", old_class, new_class);
    }

    if (cur.personality != next.personality)
    {
        cptr old_p = birth_personality_name(cur.personality);
        cptr new_p = birth_personality_name(next.personality);
        birth_diff_add(diff_entries, diff_count, 512, "Personality", old_p, new_p);
    }

    if (cur.realm1 != next.realm1)
        birth_diff_add(diff_entries, diff_count, 512, "Realm 1",
            birth_realm_name(cur.realm1), birth_realm_name(next.realm1));
    if (cur.realm2 != next.realm2)
        birth_diff_add(diff_entries, diff_count, 512, "Realm 2",
            birth_realm_name(cur.realm2), birth_realm_name(next.realm2));
    if (cur.dragon_realm != next.dragon_realm)
        birth_diff_add(diff_entries, diff_count, 512, "Dragon Realm",
            birth_realm_name(cur.dragon_realm), birth_realm_name(next.dragon_realm));

    options_snapshot_current(&cur_opts);
    next_opts = cur_opts;
    config_for_each_line(section, slot, config_apply_snapshot_line, &next_opts);
    options_diff_page(OPT_PAGE_BIRTH, &cur_opts, &next_opts, diff_lines, &diff_line_count, 256);

    for (i = 0; i < diff_line_count; i++)
    {
        char *sep = strstr(diff_lines[i], ": ");
        char *arrow = strstr(diff_lines[i], " -> ");
        if (sep && arrow)
        {
            char label[64];
            char old_val[80];
            char new_val[80];
            size_t label_len = (size_t)(sep - diff_lines[i]);
            size_t old_len = (size_t)(arrow - (sep + 2));
            strnfmt(label, sizeof(label), "%.*s", (int)label_len, diff_lines[i]);
            strnfmt(old_val, sizeof(old_val), "%.*s", (int)old_len, sep + 2);
            strnfmt(new_val, sizeof(new_val), "%s", arrow + 4);
            birth_diff_add(diff_entries, diff_count, 512, label, old_val, new_val);
        }
        else
        {
            birth_diff_add(diff_entries, diff_count, 512, diff_lines[i], "", "");
        }
    }

    return TRUE;
}

static void config_preview_birth(int slot, void *data)
{
    config_diff_entry_t diff_entries[512];
    int diff_count = 0;

    (void)data;
    if (!config_birth_build_diff(slot, diff_entries, &diff_count))
    {
        msg_print("Slot is empty.");
        return;
    }
    config_prompt_diff_list("Birth Profile Preview", diff_entries, diff_count, FALSE);
}

void config_birth_save(void)
{
    config_slot_info_t slots[CONFIG_MAX_SLOTS];
    int slot;
    char desc[CONFIG_DESC_LEN];

    config_scan_slots("birth", slots);

    if (!config_any_slots_used(slots, FALSE))
        slot = CONFIG_FIRST_USER_SLOT;
    else if (!config_prompt_slot_ex("Save Birth Profile (choose a slot)", slots, FALSE, FALSE, FALSE, FALSE,
        config_preview_birth, NULL, &slot, NULL, NULL)) return;

    if (slot >= CONFIG_FIRST_ALL_SLOT && !get_check("This modifies an All Settings profile. Continue? "))
        return;

    if (slots[slot].used && !get_check("Overwrite existing birth profile? "))
        return;

    if (!config_prompt_description(desc, sizeof(desc))) return;
    config_dump_birth_slot(slot, desc, NULL);
    msg_print("Saved birth profile.");
}

void config_birth_load(bool allow_load)
{
    config_slot_info_t slots[CONFIG_MAX_SLOTS];
    int slot;
    birth_snapshot_t next;
    config_diff_entry_t diff_entries[512];
    int diff_count = 0;

    if (!allow_load)
    {
        msg_print("Birth profiles can only be loaded at birth or in wizard mode.");
        return;
    }

    config_scan_slots("birth", slots);
    if (!config_prompt_slot_ex("Load Birth Profile (choose a slot)", slots, FALSE, TRUE, FALSE, FALSE,
        config_preview_birth, NULL, &slot, NULL, NULL)) return;

    if (!config_birth_build_diff(slot, diff_entries, &diff_count))
    {
        msg_print("Slot is empty.");
        return;
    }

    if (!config_prompt_diff_list("Birth Profile Changes", diff_entries, diff_count, TRUE))
        return;

    if (slot >= CONFIG_FIRST_ALL_SLOT)
        msg_print("Note: This loads only birth profile data. Use the main options menu to load all settings.");

    birth_snapshot_from_file(&next, "birth", slot);
    config_for_each_line("birth", slot, config_apply_pref_line_no_birth, NULL);
    config_for_each_line("birth", slot, birth_snapshot_apply_line, &next);

    game_mode = next.game_mode;
    p_ptr->psex = next.psex;
    p_ptr->prace = next.prace;
    p_ptr->psubrace = next.psubrace;
    p_ptr->pclass = next.pclass;
    p_ptr->psubclass = next.psubclass;
    p_ptr->personality = next.personality;
    p_ptr->realm1 = next.realm1;
    p_ptr->realm2 = next.realm2;
    p_ptr->dragon_realm = next.dragon_realm;

    msg_print("Loaded birth profile.");
}

static bool config_prompt_save_past_character(cptr past_name)
{
    while (1)
    {
        int ch;
        char prompt[120];

        Term_clear();
        strnfmt(prompt, sizeof(prompt), "Save settings for %s", past_name ? past_name : "previous character");
        prt(prompt, 1, 0);
        prt("a) Save All Settings", 3, 2);
        prt("o) Save Game Options", 4, 2);
        prt("w) Save Window Flags", 5, 2);
        prt("m) Save Macros/Keymaps", 6, 2);
        prt("v) Save Visuals", 7, 2);
        prt("b) Save Birth Profile", 8, 2);
        prt("ESC) Back", 11, 2);

        ch = inkey();
        if (ch == ESCAPE) return FALSE;

        if (ch == 'a' || ch == 'A')
        {
            config_slot_info_t slots[CONFIG_MAX_SLOTS];
            int slot;
            char desc[CONFIG_DESC_LEN];

            config_options_scan_all_slots(slots);
            if (!config_prompt_slot_ex("Save All Settings (choose a slot)", slots, TRUE, FALSE, FALSE, FALSE,
                config_preview_options_all, NULL, &slot, NULL, NULL)) continue;

            if (slots[slot].used && !get_check("Overwrite existing All Settings profile? "))
                continue;

            if (!config_prompt_description(desc, sizeof(desc))) continue;
            config_dump_all_settings_slot(slot, desc, NULL);
            msg_print("Saved settings.");
            return TRUE;
        }
        else if (ch == 'o' || ch == 'O')
        {
            config_slot_info_t slots[CONFIG_MAX_SLOTS];
            int slot;
            char desc[CONFIG_DESC_LEN];
            int page;

            config_options_scan_all_slots(slots);
            if (!config_prompt_slot_ex("Save Game Options (choose a slot)", slots, FALSE, FALSE, FALSE, FALSE,
                config_preview_options_all, NULL, &slot, NULL, NULL)) continue;

            if (slot >= CONFIG_FIRST_ALL_SLOT && !get_check("This modifies an All Settings profile. Continue? "))
                continue;

            if (slots[slot].used && !get_check("Overwrite existing settings in this slot? "))
                continue;

            if (!config_prompt_description(desc, sizeof(desc))) continue;
            for (page = 1; page <= 7; page++)
                config_dump_options_page_slot(page, slot, desc, NULL);
            msg_print("Saved settings.");
            return TRUE;
        }
        else if (ch == 'w' || ch == 'W')
        {
            config_slot_info_t slots[CONFIG_MAX_SLOTS];
            int slot;
            char desc[CONFIG_DESC_LEN];

            config_scan_slots("window-flags", slots);
            if (!config_prompt_slot_ex("Save Window Flags (choose a slot)", slots, FALSE, FALSE, FALSE, FALSE,
                NULL, NULL, &slot, NULL, NULL)) continue;

            if (slot >= CONFIG_FIRST_ALL_SLOT && !get_check("This modifies an All Settings profile. Continue? "))
                continue;

            if (slots[slot].used && !get_check("Overwrite existing window flags in this slot? "))
                continue;

            if (!config_prompt_description(desc, sizeof(desc))) continue;
            config_dump_window_flags_slot(slot, desc, NULL);
            msg_print("Saved window flags.");
            return TRUE;
        }
        else if (ch == 'm' || ch == 'M' || ch == 'k' || ch == 'K')
        {
            config_macros_save();
            return TRUE;
        }
        else if (ch == 'v' || ch == 'V')
        {
            config_slot_info_t slots[CONFIG_MAX_SLOTS];
            int slot;
            char desc[CONFIG_DESC_LEN];

            config_scan_slots("visuals", slots);
            if (!config_prompt_slot_ex("Save Visuals (choose a slot)", slots, FALSE, FALSE, FALSE, FALSE,
                config_preview_visuals, NULL, &slot, NULL, NULL)) continue;

            if (slot >= CONFIG_FIRST_ALL_SLOT && !get_check("This modifies an All Settings profile. Continue? "))
                continue;

            if (slots[slot].used && !get_check("Overwrite existing visuals in this slot? "))
                continue;

            if (!config_prompt_description(desc, sizeof(desc))) continue;
            config_dump_visuals_slot(slot, desc, NULL);
            msg_print("Saved visuals.");
            return TRUE;
        }
        else if (ch == 'b' || ch == 'B')
        {
            config_slot_info_t slots[CONFIG_MAX_SLOTS];
            int slot;
            char desc[CONFIG_DESC_LEN];

            config_scan_slots("birth", slots);
            if (!config_prompt_slot_ex("Save Birth Profile (choose a slot)", slots, FALSE, FALSE, FALSE, FALSE,
                config_preview_birth, NULL, &slot, NULL, NULL)) continue;

            if (slot >= CONFIG_FIRST_ALL_SLOT && !get_check("This modifies an All Settings profile. Continue? "))
                continue;

            if (slots[slot].used && !get_check("Overwrite existing birth profile? "))
                continue;

            if (!config_prompt_description(desc, sizeof(desc))) continue;
            config_dump_birth_slot(slot, desc, NULL);
            msg_print("Saved birth profile.");
            return TRUE;
        }
        else
        {
            bell();
        }
    }
}

static bool config_prompt_birth_options_apply(int slot)
{
    options_snapshot_t cur;
    options_snapshot_t next;
    char diff_lines[512][160];
    int diff_count = 0;
    char choice;

    options_snapshot_current(&cur);
    next = cur;
    config_for_each_line("birth", slot, config_apply_snapshot_line, &next);
    options_diff_page(OPT_PAGE_BIRTH, &cur, &next, diff_lines, &diff_count, 512);

    if (diff_count <= 0) return FALSE;

    choice = config_prompt_diff("Birth Option Changes", diff_lines, diff_count, FALSE, TRUE);
    return choice == 'y';
}

static bool config_prompt_load_profile_birth(void)
{
    while (1)
    {
        int ch;
        Term_clear();
        prt("Load Settings Profile", 1, 0);
        prt("a) Load All Settings", 3, 2);
        prt("o) Load Game Options", 4, 2);
        prt("w) Load Window Flags", 5, 2);
        prt("m) Load Macros/Keymaps", 6, 2);
        prt("v) Load Visuals", 7, 2);
        prt("b) Load Birth Options", 8, 2);
        prt("ESC) Back", 11, 2);

        ch = inkey();
        if (ch == ESCAPE) return FALSE;

        if (ch == 'a' || ch == 'A')
        {
            config_slot_info_t slots[CONFIG_MAX_SLOTS];
            int slot;
            int mode = rogue_like_commands ? KEYMAP_MODE_ROGUE : KEYMAP_MODE_ORIG;
            int page;

            config_options_scan_all_slots(slots);
            if (!config_prompt_slot_ex("Load All Settings (choose a slot)", slots, TRUE, TRUE, FALSE, FALSE,
                config_preview_options_all, NULL, &slot, NULL, NULL)) continue;

            for (page = 1; page <= 7; page++)
            {
                char section[32];
                config_options_section(page, section, sizeof(section));
                config_for_each_line(section, slot, config_apply_pref_line, NULL);
            }
            config_apply_window_flags_slot(slot);
            config_apply_macros_slot(slot);
            config_apply_keymaps_slot(slot, mode);
            config_apply_visuals_slot(slot);

            if (config_prompt_birth_options_apply(slot))
                config_apply_birth_options_slot(slot);

            msg_print("Loaded settings.");
            return TRUE;
        }
        else if (ch == 'o' || ch == 'O')
        {
            config_slot_info_t slots[CONFIG_MAX_SLOTS];
            int slot;
            options_snapshot_t cur;
            options_snapshot_t next;
            char diff_lines[512][160];
            int diff_count = 0;
            int page;
            char choice;

            config_options_scan_all_slots(slots);
            if (!config_prompt_slot_ex("Load Game Options (choose a slot)", slots, FALSE, TRUE, FALSE, FALSE,
                config_preview_options_all, NULL, &slot, NULL, NULL)) continue;

            options_snapshot_current(&cur);
            next = cur;
            for (page = 1; page <= 7; page++)
            {
                char section[32];
                config_options_section(page, section, sizeof(section));
                config_for_each_line(section, slot, config_apply_snapshot_line, &next);
            }
            for (page = 1; page <= 7; page++)
                options_diff_page(page, &cur, &next, diff_lines, &diff_count, 512);

            choice = config_prompt_diff("Option Changes", diff_lines, diff_count, FALSE, TRUE);
            if (choice != 'y') continue;

            if (slot >= CONFIG_FIRST_ALL_SLOT)
                msg_print("Note: This loads only game options. Use the main options menu to load all settings.");

            for (page = 1; page <= 7; page++)
            {
                char section[32];
                config_options_section(page, section, sizeof(section));
                config_for_each_line(section, slot, config_apply_pref_line, NULL);
            }
            msg_print("Loaded settings.");
            return TRUE;
        }
        else if (ch == 'w' || ch == 'W')
        {
            config_slot_info_t slots[CONFIG_MAX_SLOTS];
            int slot;

            config_scan_slots("window-flags", slots);
            if (!config_prompt_slot_ex("Load Window Flags (choose a slot)", slots, FALSE, TRUE, FALSE, FALSE,
                NULL, NULL, &slot, NULL, NULL)) continue;

            if (slot >= CONFIG_FIRST_ALL_SLOT)
                msg_print("Note: This loads only window flags. Use the main options menu to load all settings.");

            config_apply_window_flags_slot(slot);
            msg_print("Loaded window flags.");
            return TRUE;
        }
        else if (ch == 'm' || ch == 'M' || ch == 'k' || ch == 'K')
        {
            config_macros_load();
            return TRUE;
        }
        else if (ch == 'v' || ch == 'V')
        {
            config_visuals_load();
            return TRUE;
        }
        else if (ch == 'b' || ch == 'B')
        {
            config_slot_info_t slots[CONFIG_MAX_SLOTS];
            int slot;

            config_scan_slots("birth", slots);
            if (!config_prompt_slot_ex("Load Birth Options (choose a slot)", slots, FALSE, TRUE, FALSE, FALSE,
                config_preview_birth, NULL, &slot, NULL, NULL)) continue;

            if (!config_prompt_birth_options_apply(slot)) continue;
            if (slot >= CONFIG_FIRST_ALL_SLOT)
                msg_print("Note: This loads only birth options. Use the main options menu to load all settings.");
            config_apply_birth_options_slot(slot);
            msg_print("Loaded birth options.");
            return TRUE;
        }
        else
        {
            bell();
        }
    }
}

static void config_reset_all_settings_to_defaults(void)
{
    options_snapshot_t def;
    int mode = rogue_like_commands ? KEYMAP_MODE_ROGUE : KEYMAP_MODE_ORIG;
    int term;

    options_snapshot_defaults(&def);
    options_snapshot_apply(&def);
    macro_clear_all();
    keymap_clear_mode(mode);
    reset_visuals();

    for (term = 1; term < 8; term++)
        window_flag_order_clear_term(term);
    window_flag_order_sync_all();
}

bool config_birth_settings_prompt(void)
{
    char saved_root[80];
    char current_root[80];

    if (!config_current_settings_name_root(saved_root, sizeof(saved_root))) return TRUE;
    config_name_root(current_root, sizeof(current_root), player_name);
    if (streq(saved_root, current_root)) return TRUE;

    screen_save();
    while (1)
    {
        int ch;
        char buf[120];

        Term_clear();
        strnfmt(buf, sizeof(buf), "Current Settings belong to: %s", saved_root);
        prt(buf, 1, 0);
        strnfmt(buf, sizeof(buf), "New character: %s", current_root);
        prt(buf, 2, 0);

        prt("s) Save settings for previous character", 4, 2);
        prt("c) Continue with existing settings", 5, 2);
        prt("l) Continue after loading another profile", 6, 2);
        prt("r) Continue after resetting to defaults", 7, 2);
        prt("ESC) Return to character creation", 9, 2);

        ch = inkey();
        if (ch == ESCAPE)
        {
            screen_load();
            return FALSE;
        }
        if (ch == 's' || ch == 'S')
        {
            config_prompt_save_past_character(saved_root);
            continue;
        }
        if (ch == 'c' || ch == 'C')
        {
            screen_load();
            return TRUE;
        }
        if (ch == 'l' || ch == 'L')
        {
            if (config_prompt_load_profile_birth())
            {
                screen_load();
                return TRUE;
            }
            continue;
        }
        if (ch == 'r' || ch == 'R')
        {
            config_reset_all_settings_to_defaults();
            msg_print("Settings reset to defaults.");
            screen_load();
            return TRUE;
        }
        bell();
    }
}


/*
 * Modify the "window" options
 */
static void do_cmd_options_win(void)
{
    int i, j, d;

    int y = 0;
    int x = 0;

    char ch;

    bool go = TRUE;
    bool order_changed[8];
    bool pref_dirty = FALSE;

    u32b old_flag[8];


    /* Memorize old flags */
    for (j = 0; j < 8; j++)
    {
        /* Acquire current flags */
        old_flag[j] = window_flag[j];
        order_changed[j] = FALSE;
    }


    /* Clear screen */
    Term_clear();

    /* Interact */
    while (go)
    {
        /* Prompt XXX XXX XXX */
        prt("Window Flags (<dir>, t, y, n, SPACE, ESC) ", 0, 0);


        /* Display the windows */
        for (j = 0; j < 8; j++)
        {
            byte a = TERM_WHITE;

            cptr s = angband_term_name[j];

            /* Use color */
            if (j == x) a = TERM_L_BLUE;

            /* Window name, staggered, centered */
            Term_putstr(35 + j * 5 - strlen(s) / 2, 2 + j % 2, -1, a, s);

        }

        /* Display the options */
        for (i = 0; i < 16; i++)
        {
            byte a = TERM_WHITE;

            cptr str = window_flag_desc[i];

            /* Use color */
            if (i == y) a = TERM_L_BLUE;

            /* Unused option */
            if (!str) str = "(Unused option)";


            /* Flag name */
            Term_putstr(0, i + 5, -1, a, str);

            /* Display the windows */
            for (j = 0; j < 8; j++)
            {
                byte a = TERM_WHITE;

                char c = '.';

                /* Use color */
                if ((i == y) && (j == x)) a = TERM_L_BLUE;

                /* Active flag */
                if (window_flag[j] & (1L << i)) c = 'X';

                /* Flag value */
                Term_putch(35 + j * 5, i + 5, a, c);
            }
        }

        /* Place Cursor */
        Term_gotoxy(35 + x * 5, y + 5);

        /* Get key */
        ch = inkey();

        /* Analyze */
        switch (ch)
        {
            case ESCAPE:
            {
                go = FALSE;
                break;
            }

            case 'T':
            case 't':
            {
                for (j = 1; j < 8; j++)
                {
                    if (window_flag[j] & (1L << y))
                        order_changed[j] = TRUE;
                }
                if (x > 0 && window_flag[x])
                    order_changed[x] = TRUE;

                window_flag_order_clear_flag(y);
                window_flag_order_clear_term(x);
            }   /* Fall through */
            case 'y':
            case 'Y':
            {
                /* Ignore screen */
                if (x == 0) break;

                /* Set flag */
                if (!(window_flag[x] & (1L << y)))
                    order_changed[x] = TRUE;
                window_flag_order_add(x, y);
                break;
            }

            case 'n':
            case 'N':
            {
                /* Clear flag */
                if (window_flag[x] & (1L << y))
                    order_changed[x] = TRUE;
                window_flag_order_remove(x, y);
                break;
            }

            case ' ':
            {
                if (window_flag[x] & (1L << y))
                {
                    order_changed[x] = TRUE;
                    window_flag_order_remove(x, y);
                }
                else
                {
                    if (x == 0) break;
                    order_changed[x] = TRUE;
                    window_flag_order_add(x, y);
                }
                break;
            }

            case '?':
            {
                doc_display_help("option.txt", "Window");
                Term_clear();
                break;
            }

            default:
            {
                d = get_keymap_dir(ch, FALSE);

                x = (x + ddx[d] + 8) % 8;
                y = (y + ddy[d] + 16) % 16;

                if (!d) bell();
            }
        }
    }

    /* Notice changes */
    for (j = 0; j < 8; j++)
    {
        term *old = Term;
        bool changed = FALSE;

        /* Dead window */
        if (!angband_term[j]) continue;

        /* Ignore non-changes */
        if (window_flag[j] != old_flag[j]) changed = TRUE;
        if (order_changed[j]) changed = TRUE;
        if (!changed) continue;

        if (order_changed[j])
            window_flag_order_set_active(j, 0);

        /* Activate */
        Term_activate(angband_term[j]);

        /* Erase */
        Term_clear();

        /* Refresh */
        Term_fresh();

        /* Restore */
        Term_activate(old);

        p_ptr->window |= window_flag_active[j];
    }

    if (p_ptr->window) window_stuff();

    for (j = 1; j < 8; j++)
    {
        if (window_flag[j] != old_flag[j]) pref_dirty = TRUE;
        if (order_changed[j]) pref_dirty = TRUE;
    }

    if (pref_dirty) window_flag_dump();
}



#define OPT_NUM 14

static struct opts
{
    char key;
    cptr name;
    int row;
}
option_fields[OPT_NUM] =
{
    { '1', "Input Options", 3 },
    { '2', "Map Screen Options", 4 },
    { '3', "Text Display Options", 5 },
    { '4', "Game-Play Options", 6 },
    { '5', "Disturbance Options", 7 },
    { '6', "Auto-Destroyer Options", 8 },
    { '7', "List Display Options", 9 },

    { 'l', "Load All Settings", 11 },
    { 's', "Save All Settings", 12 },
    { 'r', "Reset All Settings", 13 },
    { 'p', "Mogaminator Preferences", 15 },
    { 'w', "Window Flags", 16 },

    { 'b', "Birth Options (Browse Only)", 18 },
    { 'c', "Cheat Options", 19 },
};


/*
 * Set or unset various options.
 *
 * The user must use the "Ctrl-R" command to "adapt" to changes
 * in any options which control "visual" aspects of the game.
 */
void do_cmd_options(void)
{
    char k;
    int i, d, skey;
    int y = 0;
    bool old_easy_mimics = easy_mimics;

    /* Save the screen */
    screen_save();

    /* Interact */
    while (1)
    {
        int n = OPT_NUM;

        /* Does not list cheat option when cheat option is off */
        if (!p_ptr->noscore && !allow_debug_opts) n--;

        /* Clear screen */
        Term_clear();

        /* Why are we here */
        prt("FrogComposband Options", 1, 0);

        while(1)
        {
            /* Give some choices */
            for (i = 0; i < n; i++)
            {
                byte a = TERM_WHITE;
                if (i == y) a = TERM_L_BLUE;
#ifndef ALLOW_WIZARD
                if (option_fields[i].key == 'c') continue;
#endif
                Term_putstr(5, option_fields[i].row, -1, a,
                    format("(%c) %s", toupper(option_fields[i].key), option_fields[i].name));
            }

            prt("Move to <dir>, Select to Enter, Cancel to ESC, ? to help: ", 21, 0);

            /* Get command */
            skey = inkey_special(TRUE);
            if (!(skey & SKEY_MASK)) k = (char)skey;
            else k = 0;

            /* Exit */
            if (IS_ESCAPE(k)) break;

            if (my_strchr("\n\r ", k))
            {
                k = option_fields[y].key;
                break;
            }

            for (i = 0; i < n; i++)
            {
                if (tolower(k) == option_fields[i].key) break;
            }

            /* Command is found */
            if (i < n) break;

            /* Hack -- browse help */
            if (k == '?') break;

            /* Move cursor */
            d = 0;
            if (skey == SKEY_UP) d = 8;
            if (skey == SKEY_DOWN) d = 2;
            y = (y + ddy[d] + n) % n;
            if (!d) bell();
        }

        /* Exit */
        if (IS_ESCAPE(k)) break;

        /* Analyze */
        switch (k)
        {
            case '1':
            {
                /* Process the general options */
                do_cmd_options_aux(OPT_PAGE_INPUT, "Input Options");
                break;
            }

            case '2':
            {
                /* Process the general options */
                do_cmd_options_aux(OPT_PAGE_MAPSCREEN, "Map Screen Options");
                break;
            }

            case '3':
            {
                /* Spawn */
                do_cmd_options_aux(OPT_PAGE_TEXT, "Text Display Options");
                break;
            }

            case '4':
            {
                /* Spawn */
                do_cmd_options_aux(OPT_PAGE_GAMEPLAY, "Game-Play Options");
                break;
            }

            case '5':
            {
                /* Spawn */
                do_cmd_options_aux(OPT_PAGE_DISTURBANCE, "Disturbance Options");
                break;
            }

            case '6':
            {
                /* Spawn */
                do_cmd_options_aux(OPT_PAGE_AUTODESTROY, "Auto-Destroyer Options");
                break;
            }

            case '7':
            {
                /* Spawn */
                do_cmd_options_aux(OPT_PAGE_LIST, "List Display Options");
                break;
            }

            case 'l':
            case 'L':
            {
                config_options_load_all();
                break;
            }

            case 's':
            case 'S':
            {
                config_options_save_all();
                break;
            }

            case 'r':
            case 'R':
            {
                config_options_reset_all();
                break;
            }

            /* Birth Options */
            case 'B':
            case 'b':
            {
                do_cmd_options_aux(OPT_PAGE_BIRTH, (!p_ptr->wizard || !allow_debug_opts) ? "Birth Options(browse only)" : "Birth Options((*)s effect score)");
                break;
            }

            /* Cheating Options */
            case 'C':
            {
#ifdef ALLOW_WIZARD
                if (!p_ptr->noscore && !allow_debug_opts)
                {
                    /* Cheat options are not permitted */
                    bell();
                    break;
                }

                /* Spawn */
                do_cmd_options_cheat("Cheaters never win");
#else
                bell();
#endif
                break;
            }

            /* Window flags */
            case 'W':
            case 'w':
            {
                /* Spawn */
                do_cmd_options_win();
                p_ptr->window |= (PW_INVEN | PW_EQUIP | PW_SPELL |
                          PW_MONSTER_LIST | PW_OBJECT_LIST | PW_MESSAGE | PW_OVERHEAD |
                          PW_MONSTER | PW_OBJECT | PW_SNAPSHOT |
                          PW_BORG_1 | PW_BORG_2 | PW_DUNGEON);
                break;
            }

            /* Auto-picker/destroyer editor */
            case 'P':
            case 'p':
            {
                do_cmd_edit_autopick();
                break;
            }

            case '?':
                doc_display_help("option.txt", NULL);
                Term_clear();
                break;

            /* Unknown option */
            default:
            {
                /* Oops */
                bell();
                break;
            }
        }

        /* Flush messages */
        msg_print(NULL);
    }

    /* Big fat hack */
    if (easy_mimics || old_easy_mimics) toggle_easy_mimics(easy_mimics);

    /* Restore the screen */
    screen_load();

    /* Hack - Redraw equippy chars */
    p_ptr->redraw |= (PR_EQUIPPY);
}



/*
 * Ask for a "user pref line" and process it
 *
 * XXX XXX XXX Allow absolute file names?
 */
void do_cmd_pref(void)
{
    char buf[80];

    /* Default */
    strcpy(buf, "");

    /* Ask for a "user pref command" */
    if (!get_string("Pref: ", buf, 80)) return;


    /* Process that pref command */
    (void)process_pref_file_command(buf);
}

void do_cmd_reload_autopick(void)
{
    if (!get_check("Reload auto-pick preference file? ")) return;

    /* Load the file with messages */
    autopick_load_pref(ALP_DISP_MES);
}

#ifdef ALLOW_MACROS

/*
 * Hack -- append all current macros to the given file
 */
static errr macro_dump(cptr fname)
{
    static cptr mark = "Macro Dump";

    int i;

    char buf[1024];

    /* Build the filename */
    path_build(buf, sizeof(buf), ANGBAND_DIR_USER, fname);

    /* File type is "TEXT" */
    FILE_TYPE(FILE_TYPE_TEXT);

    /* Append to the file */
    if (!open_auto_dump(buf, mark)) return (-1);

    /* Start dumping */
    auto_dump_printf("\n# Automatic macro dump\n\n");

    /* Dump them */
    for (i = 0; i < macro__num; i++)
    {
        /* Extract the action */
        ascii_to_text(buf, macro__act[i]);

        /* Dump the macro */
        auto_dump_printf("A:%s\n", buf);

        /* Extract the action */
        ascii_to_text(buf, macro__pat[i]);

        /* Dump normal macros */
        auto_dump_printf("P:%s\n", buf);

        /* End the macro */
        auto_dump_printf("\n");
    }

    /* Close */
    close_auto_dump();

    /* Success */
    return (0);
}


/*
 * Hack -- ask for a "trigger" (see below)
 *
 * Note the complex use of the "inkey()" function from "util.c".
 *
 * Note that both "flush()" calls are extremely important.
 */
static void do_cmd_macro_aux(char *buf)
{
    int i, n = 0;

    char tmp[1024];


    /* Flush */
    flush();

    /* Do not process macros */
    inkey_base = TRUE;

    /* First key */
    i = inkey();

    /* Read the pattern */
    while (i)
    {
        /* Save the key */
        buf[n++] = i;

        /* Do not process macros */
        inkey_base = TRUE;

        /* Do not wait for keys */
        inkey_scan = TRUE;

        /* Attempt to read a key */
        i = inkey();
    }

    /* Terminate */
    buf[n] = '\0';

    /* Flush */
    flush();


    /* Convert the trigger */
    ascii_to_text(tmp, buf);

    /* Hack -- display the trigger */
    Term_addstr(-1, TERM_WHITE, tmp);
}

#endif


/*
 * Hack -- ask for a keymap "trigger" (see below)
 *
 * Note that both "flush()" calls are extremely important. This may
 * no longer be true, since "util.c" is much simpler now. XXX XXX XXX
 */
static void do_cmd_macro_aux_keymap(char *buf)
{
    char tmp[1024];


    /* Flush */
    flush();


    /* Get a key */
    buf[0] = inkey();
    buf[1] = '\0';


    /* Convert to ascii */
    ascii_to_text(tmp, buf);

    /* Hack -- display the trigger */
    Term_addstr(-1, TERM_WHITE, tmp);


    /* Flush */
    flush();
}


/*
 * Hack -- append all keymaps to the given file
 */
static errr keymap_dump(cptr fname)
{
    static cptr mark = "Keymap Dump";
    int i;

    char key[1024];
    char buf[1024];

    int mode;

    /* Roguelike */
    if (rogue_like_commands)
    {
        mode = KEYMAP_MODE_ROGUE;
    }

    /* Original */
    else
    {
        mode = KEYMAP_MODE_ORIG;
    }


    /* Build the filename */
    path_build(buf, sizeof(buf), ANGBAND_DIR_USER, fname);

    /* File type is "TEXT" */
    FILE_TYPE(FILE_TYPE_TEXT);

    /* Append to the file */
    if (!open_auto_dump(buf, mark)) return -1;

    /* Start dumping */
    auto_dump_printf("\n# Automatic keymap dump\n\n");

    /* Dump them */
    for (i = 0; i < 256; i++)
    {
        cptr act;

        /* Loop up the keymap */
        act = keymap_act[mode][i];

        /* Skip empty keymaps */
        if (!act) continue;

        /* Encode the key */
        buf[0] = i;
        buf[1] = '\0';
        ascii_to_text(key, buf);

        /* Encode the action */
        ascii_to_text(buf, act);

        /* Dump the macro */
        auto_dump_printf("A:%s\n", buf);
        auto_dump_printf("C:%d:%s\n", mode, key);
    }

    /* Close */
    close_auto_dump();

    /* Success */
    return (0);
}



/*
 * Interact with "macros"
 *
 * Note that the macro "action" must be defined before the trigger.
 *
 * Could use some helpful instructions on this page. XXX XXX XXX
 */
void do_cmd_macros(void)
{
    int i;

    char tmp[1024];

    char buf[1024];

    int mode;
    macro_snapshot_t macro_before;
    keymap_snapshot_t keymap_before;


    /* Roguelike */
    if (rogue_like_commands)
    {
        mode = KEYMAP_MODE_ROGUE;
    }

    /* Original */
    else
    {
        mode = KEYMAP_MODE_ORIG;
    }

    macro_snapshot_current(&macro_before);
    keymap_snapshot_current(&keymap_before, mode);

    /* File type is "TEXT" */
    FILE_TYPE(FILE_TYPE_TEXT);


    /* Save screen */
    screen_save();


    /* Process requests until done */
    while (1)
    {
        /* Clear screen */
        Term_clear();

        /* Describe */
        prt("Interact with Macros", 2, 0);



        /* Describe that action */
        prt("Current action (if any) shown below:", 20, 0);


        /* Analyze the current action */
        ascii_to_text(buf, macro__buf);

        /* Display the current action */
        prt(buf, 22, 0);


        /* Selections */
        prt("(1) Load a user pref file", 4, 5);

#ifdef ALLOW_MACROS
        prt("(2) Append macros and keymaps to a file", 5, 5);
        prt("(3) Query a macro", 6, 5);
        prt("(4) Create a macro", 7, 5);
        prt("(5) Remove a macro", 8, 5);
        prt("(7) Query a keymap", 10, 5);
        prt("(8) Create a keymap", 11, 5);
        prt("(9) Remove a keymap", 12, 5);
        prt("(0) Enter a new action", 13, 5);
        prt("(L) Load macros/keymaps from config profile", 14, 5);
        prt("(S) Save macros/keymaps to config profile", 15, 5);

#endif /* ALLOW_MACROS */

        /* Prompt */
        prt("Command: ", 19, 0);


        /* Get a command */
        i = inkey();

        /* Leave */
        if (i == ESCAPE) break;

        /* Load a 'macro' file */
        else if (i == '1')
        {
            errr err;

            /* Prompt */
            prt("Command: Load a user pref file", 19, 0);


            /* Prompt */
            prt("File: ", 21, 0);


            /* Default filename */
            sprintf(tmp, "%s.prf", pref_save_base);

            /* Ask for a file */
            if (!askfor(tmp, 80)) continue;

            /* Process the given filename */
            err = process_pref_file(tmp);
            if (-2 == err)
            {
                msg_format("Loaded default '%s'.", tmp);
            }
            else if (err)
            {
                /* Prompt */
                msg_format("Failed to load '%s'!", tmp);
            }
            else
            {
                msg_format("Loaded '%s'.", tmp);
            }
        }

#ifdef ALLOW_MACROS

        /* Save macros */
        else if (i == '2')
        {
            /* Prompt */
            prt("Command: Append macros and keymaps to a file", 19, 0);


            /* Prompt */
            prt("File: ", 21, 0);


            /* Default filename */
            sprintf(tmp, "%s.prf", pref_save_base);

            /* Ask for a file */
            if (!askfor(tmp, 80)) continue;

            /* Dump the macros */
            (void)macro_dump(tmp);
            (void)keymap_dump(tmp);

            /* Prompt */
            msg_print("Appended macros and keymaps.");

        }

        /* Query a macro */
        else if (i == '3')
        {
            int k;

            /* Prompt */
            prt("Command: Query a macro", 19, 0);


            /* Prompt */
            prt("Trigger: ", 21, 0);


            /* Get a macro trigger */
            do_cmd_macro_aux(buf);

            /* Acquire action */
            k = macro_find_exact(buf);

            /* Nothing found */
            if (k < 0)
            {
                /* Prompt */
                msg_print("Found no macro.");

            }

            /* Found one */
            else
            {
                /* Obtain the action */
                strcpy(macro__buf, macro__act[k]);

                /* Analyze the current action */
                ascii_to_text(buf, macro__buf);

                /* Display the current action */
                prt(buf, 22, 0);

                /* Prompt */
                msg_print("Found a macro.");

            }
        }

        /* Create a macro */
        else if (i == '4')
        {
            /* Prompt */
            prt("Command: Create a macro", 19, 0);


            /* Prompt */
            prt("Trigger: ", 21, 0);


            /* Get a macro trigger */
            do_cmd_macro_aux(buf);

            /* Clear */
            clear_from(20);

            /* Help message */
            c_prt(TERM_L_RED, "Press Left/Right arrow keys to move cursor. Backspace/Delete to delete a char.", 22, 0);

            /* Prompt */
            prt("Action: ", 20, 0);


            /* Convert to text */
            ascii_to_text(tmp, macro__buf);

            /* Get an encoded action */
            if (askfor(tmp, 80))
            {
                /* Convert to ascii */
                text_to_ascii(macro__buf, tmp);

                /* Link the macro */
                macro_add(buf, macro__buf);

                /* Prompt */
                msg_print("Added a macro.");

            }
        }

        /* Remove a macro */
        else if (i == '5')
        {
            /* Prompt */
            prt("Command: Remove a macro", 19, 0);


            /* Prompt */
            prt("Trigger: ", 21, 0);


            /* Get a macro trigger */
            do_cmd_macro_aux(buf);

            /* Link the macro */
            macro_add(buf, buf);

            /* Prompt */
            msg_print("Removed a macro.");

        }

        /* Query a keymap */
        else if (i == '7')
        {
            cptr act;

            /* Prompt */
            prt("Command: Query a keymap", 19, 0);


            /* Prompt */
            prt("Keypress: ", 21, 0);


            /* Get a keymap trigger */
            do_cmd_macro_aux_keymap(buf);

            /* Look up the keymap */
            act = keymap_act[mode][(byte)(buf[0])];

            /* Nothing found */
            if (!act)
            {
                /* Prompt */
                msg_print("Found no keymap.");

            }

            /* Found one */
            else
            {
                /* Obtain the action */
                strcpy(macro__buf, act);

                /* Analyze the current action */
                ascii_to_text(buf, macro__buf);

                /* Display the current action */
                prt(buf, 22, 0);

                /* Prompt */
                msg_print("Found a keymap.");

            }
        }

        /* Create a keymap */
        else if (i == '8')
        {
            /* Prompt */
            prt("Command: Create a keymap", 19, 0);


            /* Prompt */
            prt("Keypress: ", 21, 0);


            /* Get a keymap trigger */
            do_cmd_macro_aux_keymap(buf);

            /* Clear */
            clear_from(20);

            /* Help message */
            c_prt(TERM_L_RED, "Press Left/Right arrow keys to move cursor. Backspace/Delete to delete a char.", 22, 0);

            /* Prompt */
            prt("Action: ", 20, 0);


            /* Convert to text */
            ascii_to_text(tmp, macro__buf);

            /* Get an encoded action */
            if (askfor(tmp, 80))
            {
                /* Convert to ascii */
                text_to_ascii(macro__buf, tmp);

                /* Free old keymap */
                z_string_free(keymap_act[mode][(byte)(buf[0])]);

                /* Make new keymap */
                keymap_act[mode][(byte)(buf[0])] = z_string_make(macro__buf);

                /* Prompt */
                msg_print("Added a keymap.");

            }
        }

        /* Remove a keymap */
        else if (i == '9')
        {
            /* Prompt */
            prt("Command: Remove a keymap", 19, 0);


            /* Prompt */
            prt("Keypress: ", 21, 0);


            /* Get a keymap trigger */
            do_cmd_macro_aux_keymap(buf);

            /* Free old keymap */
            z_string_free(keymap_act[mode][(byte)(buf[0])]);

            /* Make new keymap */
            keymap_act[mode][(byte)(buf[0])] = NULL;

            /* Prompt */
            msg_print("Removed a keymap.");

        }

        /* Enter a new action */
        else if (i == '0')
        {
            /* Prompt */
            prt("Command: Enter a new action", 19, 0);

            /* Clear */
            clear_from(20);

            /* Help message */
            c_prt(TERM_L_RED, "Press Left/Right arrow keys to move cursor. Backspace/Delete to delete a char.", 22, 0);

            /* Prompt */
            prt("Action: ", 20, 0);

            /* Hack -- limit the value */
            tmp[80] = '\0';

            /* Get an encoded action */
            if (!askfor(buf, 80)) continue;

            /* Extract an action */
            text_to_ascii(macro__buf, buf);
        }

#endif /* ALLOW_MACROS */

        else if (i == 'L' || i == 'l' || i == 'K' || i == 'k')
        {
            config_macros_load();
        }

        else if (i == 'S' || i == 's' || i == 'M' || i == 'm')
        {
            config_macros_save();
        }

        /* Oops */
        else
        {
            /* Oops */
            bell();
        }

        /* Flush messages */
        msg_print(NULL);
    }

    /* Load screen */
    screen_load();

    {
        macro_snapshot_t macro_after;
        keymap_snapshot_t keymap_after;
        config_diff_entry_t diff_entries[512];
        int diff_count = 0;
        bool changed = FALSE;

        macro_snapshot_current(&macro_after);
        config_macro_diff(&macro_before, &macro_after, diff_entries, &diff_count, 512);
        if (diff_count > 0) changed = TRUE;
        diff_count = 0;

        keymap_snapshot_current(&keymap_after, mode);
        config_keymap_diff(&keymap_before, &keymap_after, diff_entries, &diff_count, 512);
        if (diff_count > 0) changed = TRUE;

        macro_snapshot_free(&macro_after);
        keymap_snapshot_free(&keymap_after);

        if (changed)
        {
            while (1)
            {
                int ch;
                prt("Save macro/keymap changes to Current Settings? (y/n/r)", 0, 0);
                ch = inkey();
                if (ch == 'y' || ch == 'Y')
                {
                    config_save_current_macros();
                    config_save_current_keymaps(mode);
                    msg_print("Saved to Current Settings.");
                    break;
                }
                if (ch == 'r' || ch == 'R')
                {
                    macro_snapshot_apply(&macro_before);
                    keymap_snapshot_apply(&keymap_before);
                    msg_print("Reverted macro/keymap changes.");
                    break;
                }
                if (ch == 'n' || ch == 'N' || ch == ESCAPE)
                    break;
                bell();
            }
        }
    }

    macro_snapshot_free(&macro_before);
    keymap_snapshot_free(&keymap_before);
}


static cptr lighting_level_str[F_LIT_MAX] =
{
    "standard",
    "brightly lit",
    "darkened",
};


static bool cmd_visuals_aux(int i, int *num, int max)
{
    if (iscntrl(i))
    {
        char str[10] = "";
        int tmp;

        sprintf(str, "%d", *num);

        if (!get_string(format("Input new number(0-%d): ", max-1), str, 5))
            return FALSE;

        tmp = strtol(str, NULL, 0);
        if (tmp >= 0 && tmp < max)
            *num = tmp;
    }
    else if (isupper(i))
        *num = (*num + max - 1) % max;
    else
        *num = (*num + 1) % max;

    return TRUE;
}

static void print_visuals_menu(cptr choice_msg)
{
    prt("Interact with Visuals", 1, 0);

    /* Give some choices */
    prt("(0) Load a user pref file", 3, 5);

#ifdef ALLOW_VISUALS
    prt("(1) Dump monster attr/chars", 4, 5);
    prt("(2) Dump object attr/chars", 5, 5);
    prt("(3) Dump feature attr/chars", 6, 5);
    prt("(4) Change monster attr/chars (numeric operation)", 7, 5);
    prt("(5) Change object attr/chars (numeric operation)", 8, 5);
    prt("(6) Change feature attr/chars (numeric operation)", 9, 5);
    prt("(7) Change monster attr/chars (visual mode)", 10, 5);
    prt("(8) Change object attr/chars (visual mode)", 11, 5);
    prt("(9) Change feature attr/chars (visual mode)", 12, 5);

#endif /* ALLOW_VISUALS */

    prt("(R) Reset visuals", 13, 5);
    prt("(L) Load visuals from config profile", 14, 5);
    prt("(S) Save visuals to config profile", 15, 5);

    /* Prompt */
    prt(format("Command: %s", choice_msg ? choice_msg : ""), 17, 0);
}

typedef struct visuals_snapshot_s visuals_snapshot_t;
struct visuals_snapshot_s
{
    int max_r;
    int max_k;
    int max_f;
    byte *r_attr;
    byte *r_char;
    byte *k_attr;
    byte *k_char;
    byte *f_attr;
    byte *f_char;
};

static void visuals_snapshot_init(visuals_snapshot_t *snap)
{
    int i;
    snap->max_r = max_r_idx;
    snap->max_k = max_k_idx;
    snap->max_f = max_f_idx;

    C_MAKE(snap->r_attr, snap->max_r, byte);
    C_MAKE(snap->r_char, snap->max_r, byte);
    C_MAKE(snap->k_attr, snap->max_k, byte);
    C_MAKE(snap->k_char, snap->max_k, byte);
    C_MAKE(snap->f_attr, snap->max_f * F_LIT_MAX, byte);
    C_MAKE(snap->f_char, snap->max_f * F_LIT_MAX, byte);

    for (i = 0; i < snap->max_r; i++)
    {
        monster_race *r_ptr = &r_info[i];
        snap->r_attr[i] = r_ptr->x_attr;
        snap->r_char[i] = r_ptr->x_char;
    }
    for (i = 0; i < snap->max_k; i++)
    {
        object_kind *k_ptr = &k_info[i];
        snap->k_attr[i] = k_ptr->x_attr;
        snap->k_char[i] = k_ptr->x_char;
    }
    for (i = 0; i < snap->max_f; i++)
    {
        feature_type *f_ptr = &f_info[i];
        int lit;
        for (lit = 0; lit < F_LIT_MAX; lit++)
        {
            int idx = i * F_LIT_MAX + lit;
            snap->f_attr[idx] = f_ptr->x_attr[lit];
            snap->f_char[idx] = f_ptr->x_char[lit];
        }
    }
}

static void visuals_snapshot_restore(const visuals_snapshot_t *snap)
{
    int i;
    int max_r = MIN(max_r_idx, snap->max_r);
    int max_k = MIN(max_k_idx, snap->max_k);
    int max_f = MIN(max_f_idx, snap->max_f);

    for (i = 0; i < max_r; i++)
    {
        monster_race *r_ptr = &r_info[i];
        r_ptr->x_attr = snap->r_attr[i];
        r_ptr->x_char = snap->r_char[i];
    }
    for (i = 0; i < max_k; i++)
    {
        object_kind *k_ptr = &k_info[i];
        k_ptr->x_attr = snap->k_attr[i];
        k_ptr->x_char = snap->k_char[i];
    }
    for (i = 0; i < max_f; i++)
    {
        feature_type *f_ptr = &f_info[i];
        int lit;
        for (lit = 0; lit < F_LIT_MAX; lit++)
        {
            int idx = i * F_LIT_MAX + lit;
            f_ptr->x_attr[lit] = snap->f_attr[idx];
            f_ptr->x_char[lit] = snap->f_char[idx];
        }
    }
}

static void visuals_snapshot_free(visuals_snapshot_t *snap)
{
    C_KILL(snap->r_attr, snap->max_r, byte);
    C_KILL(snap->r_char, snap->max_r, byte);
    C_KILL(snap->k_attr, snap->max_k, byte);
    C_KILL(snap->k_char, snap->max_k, byte);
    C_KILL(snap->f_attr, snap->max_f * F_LIT_MAX, byte);
    C_KILL(snap->f_char, snap->max_f * F_LIT_MAX, byte);
}

static void do_cmd_knowledge_monsters(bool *need_redraw, bool visual_only, int direct_r_idx);
static void do_cmd_knowledge_objects(bool *need_redraw, bool visual_only, int direct_k_idx);
static void do_cmd_knowledge_features(bool *need_redraw, bool visual_only, int direct_f_idx, int *lighting_level);

/*
 * Interact with "visuals"
 */
void do_cmd_visuals(void)
{
    int i;
    char tmp[160];
    char buf[1024];
    bool need_redraw = FALSE;
    bool visuals_changed = FALSE;
    visuals_snapshot_t snapshot;
    const char *empty_symbol = "<< ? >>";

    if (use_bigtile) empty_symbol = "<< ?? >>";

    /* File type is "TEXT" */
    FILE_TYPE(FILE_TYPE_TEXT);

    /* Save the screen */
    screen_save();
    visuals_snapshot_init(&snapshot);

    /* Interact until done */
    while (1)
    {
        /* Clear screen */
        Term_clear();

        /* Ask for a choice */
        print_visuals_menu(NULL);

        /* Prompt */
        i = inkey();

        /* Done */
        if (i == ESCAPE) break;

        switch (i)
        {
        /* Load a 'pref' file */
        case '0':
            /* Prompt */
            prt("Command: Load a user pref file", 15, 0);

            /* Prompt */
            prt("File: ", 17, 0);

            /* Default filename */
            sprintf(tmp, "%s.prf", pref_save_base);

            /* Query */
            if (!askfor(tmp, 70)) continue;

            /* Process the given filename */
            (void)process_pref_file(tmp);

            need_redraw = TRUE;
            visuals_changed = TRUE;
            break;

#ifdef ALLOW_VISUALS

        /* Dump monster attr/chars */
        case '1':
        {
            static cptr mark = "Monster attr/chars";

            /* Prompt */
            prt("Command: Dump monster attr/chars", 15, 0);

            /* Prompt */
            prt("File: ", 17, 0);

            /* Default filename */
            sprintf(tmp, "%s.prf", pref_save_base);

            /* Get a filename */
            if (!askfor(tmp, 70)) continue;

            /* Build the filename */
            path_build(buf, sizeof(buf), ANGBAND_DIR_USER, tmp);

            /* Append to the file */
            if (!open_auto_dump(buf, mark)) continue;

            /* Start dumping */
            auto_dump_printf("\n# Monster attr/char definitions\n\n");

            /* Dump monsters */
            for (i = 0; i < max_r_idx; i++)
            {
                monster_race *r_ptr = &r_info[i];

                /* Skip non-entries */
                if (!r_ptr->name) continue;

                /* Dump a comment */
                auto_dump_printf("# %s\n", (r_name + r_ptr->name));

                /* Dump the monster attr/char info */
                auto_dump_printf("R:%d:0x%02X/0x%02X\n\n", i,
                    (byte)(r_ptr->x_attr), (byte)(r_ptr->x_char));
            }

            /* Close */
            close_auto_dump();

            /* Message */
            msg_print("Dumped monster attr/chars.");

            break;
        }

        /* Dump object attr/chars */
        case '2':
        {
            static cptr mark = "Object attr/chars";

            /* Prompt */
            prt("Command: Dump object attr/chars", 15, 0);

            /* Prompt */
            prt("File: ", 17, 0);

            /* Default filename */
            sprintf(tmp, "%s.prf", pref_save_base);

            /* Get a filename */
            if (!askfor(tmp, 70)) continue;

            /* Build the filename */
            path_build(buf, sizeof(buf), ANGBAND_DIR_USER, tmp);

            /* Append to the file */
            if (!open_auto_dump(buf, mark)) continue;

            /* Start dumping */
            auto_dump_printf("\n# Object attr/char definitions\n\n");

            /* Dump objects */
            for (i = 0; i < max_k_idx; i++)
            {
                char o_name[80];
                object_kind *k_ptr = &k_info[i];

                /* Skip non-entries */
                if (!k_ptr->name) continue;

                if (!k_ptr->flavor)
                {
                    /* Tidy name */
                    strip_name(o_name, i);
                }
                else
                {
                    object_type forge;

                    /* Prepare dummy object */
                    object_prep(&forge, i);

                    /* Get un-shuffled flavor name */
                    object_desc(o_name, &forge, OD_FORCE_FLAVOR);
                }

                /* Dump a comment */
                auto_dump_printf("# %s\n", o_name);

                /* Dump the object attr/char info */
                auto_dump_printf("K:%d:%d:0x%02X/0x%02X\n\n",
                    k_ptr->tval, k_ptr->sval,
                    (byte)(k_ptr->x_attr), (byte)(k_ptr->x_char));
            }

            /* Close */
            close_auto_dump();

            /* Message */
            msg_print("Dumped object attr/chars.");

            break;
        }

        /* Dump feature attr/chars */
        case '3':
        {
            static cptr mark = "Feature attr/chars";

            /* Prompt */
            prt("Command: Dump feature attr/chars", 15, 0);

            /* Prompt */
            prt("File: ", 17, 0);

            /* Default filename */
            sprintf(tmp, "%s.prf", pref_save_base);

            /* Get a filename */
            if (!askfor(tmp, 70)) continue;

            /* Build the filename */
            path_build(buf, sizeof(buf), ANGBAND_DIR_USER, tmp);

            /* Append to the file */
            if (!open_auto_dump(buf, mark)) continue;

            /* Start dumping */
            auto_dump_printf("\n# Feature attr/char definitions\n\n");

            /* Dump features */
            for (i = 0; i < max_f_idx; i++)
            {
                feature_type *f_ptr = &f_info[i];

                /* Skip non-entries */
                if (!f_ptr->name) continue;

                /* Skip mimiccing features */
                if (f_ptr->mimic != i) continue;

                /* Dump a comment */
                auto_dump_printf("# %s\n", (f_name + f_ptr->name));

                /* Dump the feature attr/char info */
                auto_dump_printf("F:%d:0x%02X/0x%02X:0x%02X/0x%02X:0x%02X/0x%02X\n\n", i,
                    (byte)(f_ptr->x_attr[F_LIT_STANDARD]), (byte)(f_ptr->x_char[F_LIT_STANDARD]),
                    (byte)(f_ptr->x_attr[F_LIT_LITE]), (byte)(f_ptr->x_char[F_LIT_LITE]),
                    (byte)(f_ptr->x_attr[F_LIT_DARK]), (byte)(f_ptr->x_char[F_LIT_DARK]));
            }

            /* Close */
            close_auto_dump();

            /* Message */
            msg_print("Dumped feature attr/chars.");

            break;
        }

        /* Modify monster attr/chars (numeric operation) */
        case '4':
        {
            static cptr choice_msg = "Change monster attr/chars";
            static int r = 0;

            prt(format("Command: %s", choice_msg), 15, 0);

            /* Hack -- query until done */
            while (1)
            {
                monster_race *r_ptr = &r_info[r];
                char c;
                int t;

                byte da = r_ptr->d_attr;
                byte dc = r_ptr->d_char;
                byte ca = r_ptr->x_attr;
                byte cc = r_ptr->x_char;

                /* Label the object */
                Term_putstr(5, 17, -1, TERM_WHITE,
                        format("Monster = %d, Name = %-40.40s",
                           r, (r_name + r_ptr->name)));

                /* Label the Default values */
                Term_putstr(10, 19, -1, TERM_WHITE,
                        format("Default attr/char = %3u / %3u", da, dc));

                Term_putstr(40, 19, -1, TERM_WHITE, empty_symbol);
                Term_queue_bigchar(43, 19, da, dc, 0, 0);

                /* Label the Current values */
                Term_putstr(10, 20, -1, TERM_WHITE,
                        format("Current attr/char = %3u / %3u", ca, cc));

                Term_putstr(40, 20, -1, TERM_WHITE, empty_symbol);
                Term_queue_bigchar(43, 20, ca, cc, 0, 0);

                /* Prompt */
                Term_putstr(0, 22, -1, TERM_WHITE,
                        "Command (n/N/^N/a/A/^A/c/C/^C/v/V/^V): ");

                /* Get a command */
                i = inkey();

                /* All done */
                if (i == ESCAPE) break;

                if (iscntrl(i)) c = 'a' + i - KTRL('A');
                else if (isupper(i)) c = 'a' + i - 'A';
                else c = i;

                switch (c)
                {
                case 'n':
                    {
                        int prev_r = r;
                        do
                        {
                            if (!cmd_visuals_aux(i, &r, max_r_idx))
                            {
                                r = prev_r;
                                break;
                            }
                        }
                        while (!r_info[r].name);
                    }
                    break;
                case 'a':
                    t = (int)r_ptr->x_attr;
                    (void)cmd_visuals_aux(i, &t, 256);
                    r_ptr->x_attr = (byte)t;
                    need_redraw = TRUE;
                    visuals_changed = TRUE;
                    break;
                case 'c':
                    t = (int)r_ptr->x_char;
                    (void)cmd_visuals_aux(i, &t, 256);
                    r_ptr->x_char = (byte)t;
                    need_redraw = TRUE;
                    visuals_changed = TRUE;
                    break;
                case 'v':
                    do_cmd_knowledge_monsters(&need_redraw, TRUE, r);

                    /* Clear screen */
                    Term_clear();
                    print_visuals_menu(choice_msg);
                    break;
                }
            }

            break;
        }

        /* Modify object attr/chars (numeric operation) */
        case '5':
        {
            static cptr choice_msg = "Change object attr/chars";
            static int k = 0;

            prt(format("Command: %s", choice_msg), 15, 0);

            /* Hack -- query until done */
            while (1)
            {
                object_kind *k_ptr = &k_info[k];
                char c;
                int t;

                byte da = k_ptr->d_attr;
                byte dc = k_ptr->d_char;
                byte ca = k_ptr->x_attr;
                byte cc = k_ptr->x_char;

                /* Label the object */
                Term_putstr(5, 17, -1, TERM_WHITE,
                        format("Object = %d, Name = %-40.40s",
                           k, k_name + (!k_ptr->flavor ? k_ptr->name : k_ptr->flavor_name)));

                /* Label the Default values */
                Term_putstr(10, 19, -1, TERM_WHITE,
                        format("Default attr/char = %3d / %3d", da, dc));

                Term_putstr(40, 19, -1, TERM_WHITE, empty_symbol);
                Term_queue_bigchar(43, 19, da, dc, 0, 0);

                /* Label the Current values */
                Term_putstr(10, 20, -1, TERM_WHITE,
                        format("Current attr/char = %3d / %3d", ca, cc));

                Term_putstr(40, 20, -1, TERM_WHITE, empty_symbol);
                Term_queue_bigchar(43, 20, ca, cc, 0, 0);

                /* Prompt */
                Term_putstr(0, 22, -1, TERM_WHITE,
                        "Command (n/N/^N/a/A/^A/c/C/^C/v/V/^V): ");

                /* Get a command */
                i = inkey();

                /* All done */
                if (i == ESCAPE) break;

                if (iscntrl(i)) c = 'a' + i - KTRL('A');
                else if (isupper(i)) c = 'a' + i - 'A';
                else c = i;

                switch (c)
                {
                case 'n':
                    {
                        int prev_k = k;
                        do
                        {
                            if (!cmd_visuals_aux(i, &k, max_k_idx))
                            {
                                k = prev_k;
                                break;
                            }
                        }
                        while (!k_info[k].name);
                    }
                    break;
                case 'a':
                    t = (int)k_ptr->x_attr;
                    (void)cmd_visuals_aux(i, &t, 256);
                    k_ptr->x_attr = (byte)t;
                    need_redraw = TRUE;
                    visuals_changed = TRUE;
                    break;
                case 'c':
                    t = (int)k_ptr->x_char;
                    (void)cmd_visuals_aux(i, &t, 256);
                    k_ptr->x_char = (byte)t;
                    need_redraw = TRUE;
                    visuals_changed = TRUE;
                    break;
                case 'v':
                    do_cmd_knowledge_objects(&need_redraw, TRUE, k);

                    /* Clear screen */
                    Term_clear();
                    print_visuals_menu(choice_msg);
                    break;
                }
            }

            break;
        }

        /* Modify feature attr/chars (numeric operation) */
        case '6':
        {
            static cptr choice_msg = "Change feature attr/chars";
            static int f = 0;
            static int lighting_level = F_LIT_STANDARD;

            prt(format("Command: %s", choice_msg), 15, 0);

            /* Hack -- query until done */
            while (1)
            {
                feature_type *f_ptr = &f_info[f];
                char c;
                int t;

                byte da = f_ptr->d_attr[lighting_level];
                byte dc = f_ptr->d_char[lighting_level];
                byte ca = f_ptr->x_attr[lighting_level];
                byte cc = f_ptr->x_char[lighting_level];

                /* Label the object */
                prt("", 17, 5);
                Term_putstr(5, 17, -1, TERM_WHITE,
                        format("Terrain = %d, Name = %s, Lighting = %s",
                           f, (f_name + f_ptr->name), lighting_level_str[lighting_level]));

                /* Label the Default values */
                Term_putstr(10, 19, -1, TERM_WHITE,
                        format("Default attr/char = %3d / %3d", da, dc));

                Term_putstr(40, 19, -1, TERM_WHITE, empty_symbol);

                Term_queue_bigchar(43, 19, da, dc, 0, 0);

                /* Label the Current values */
                Term_putstr(10, 20, -1, TERM_WHITE,
                        format("Current attr/char = %3d / %3d", ca, cc));

                Term_putstr(40, 20, -1, TERM_WHITE, empty_symbol);
                Term_queue_bigchar(43, 20, ca, cc, 0, 0);

                /* Prompt */
                Term_putstr(0, 22, -1, TERM_WHITE,
                        "Command (n/N/^N/a/A/^A/c/C/^C/l/L/^L/d/D/^D/v/V/^V): ");

                /* Get a command */
                i = inkey();

                /* All done */
                if (i == ESCAPE) break;

                if (iscntrl(i)) c = 'a' + i - KTRL('A');
                else if (isupper(i)) c = 'a' + i - 'A';
                else c = i;

                switch (c)
                {
                case 'n':
                    {
                        int prev_f = f;
                        do
                        {
                            if (!cmd_visuals_aux(i, &f, max_f_idx))
                            {
                                f = prev_f;
                                break;
                            }
                        }
                        while (!f_info[f].name || (f_info[f].mimic != f));
                    }
                    break;
                case 'a':
                    t = (int)f_ptr->x_attr[lighting_level];
                    (void)cmd_visuals_aux(i, &t, 256);
                    f_ptr->x_attr[lighting_level] = (byte)t;
                    need_redraw = TRUE;
                    visuals_changed = TRUE;
                    break;
                case 'c':
                    t = (int)f_ptr->x_char[lighting_level];
                    (void)cmd_visuals_aux(i, &t, 256);
                    f_ptr->x_char[lighting_level] = (byte)t;
                    need_redraw = TRUE;
                    visuals_changed = TRUE;
                    break;
                case 'l':
                    (void)cmd_visuals_aux(i, &lighting_level, F_LIT_MAX);
                    break;
                case 'd':
                    apply_default_feat_lighting(f_ptr->x_attr, f_ptr->x_char);
                    need_redraw = TRUE;
                    visuals_changed = TRUE;
                    break;
                case 'v':
                    do_cmd_knowledge_features(&need_redraw, TRUE, f, &lighting_level);

                    /* Clear screen */
                    Term_clear();
                    print_visuals_menu(choice_msg);
                    break;
                }
            }

            break;
        }

        /* Modify monster attr/chars (visual mode) */
        case '7':
            do_cmd_knowledge_monsters(&need_redraw, TRUE, -1);
            break;

        /* Modify object attr/chars (visual mode) */
        case '8':
            do_cmd_knowledge_objects(&need_redraw, TRUE, -1);
            break;

        /* Modify feature attr/chars (visual mode) */
        case '9':
        {
            int lighting_level = F_LIT_STANDARD;
            do_cmd_knowledge_features(&need_redraw, TRUE, -1, &lighting_level);
            break;
        }

#endif /* ALLOW_VISUALS */

        /* Reset visuals */
        case 'R':
        case 'r':
            /* Reset */
            reset_visuals();

            /* Message */
            msg_print("Visual attr/char tables reset.");

            need_redraw = TRUE;
            visuals_changed = TRUE;
            break;

        case 'L':
        case 'l':
            config_visuals_load();
            need_redraw = TRUE;
            visuals_changed = TRUE;
            break;

        case 'S':
        case 's':
            config_visuals_save();
            break;

        /* Unknown option */
        default:
            bell();
            break;
        }

        /* Flush messages */
        msg_print(NULL);
    }

    /* Restore the screen */
    screen_load();

    if (visuals_changed)
    {
        while (1)
        {
            int ch;
            prt("Save visual changes to Current Settings? (y/n/r)", 0, 0);
            ch = inkey();
            if (ch == 'y' || ch == 'Y')
            {
                config_save_current_visuals();
                msg_print("Saved to Current Settings.");
                break;
            }
            if (ch == 'r' || ch == 'R')
            {
                visuals_snapshot_restore(&snapshot);
                need_redraw = TRUE;
                msg_print("Reverted visual changes.");
                break;
            }
            if (ch == 'n' || ch == 'N' || ch == ESCAPE)
                break;
            bell();
        }
    }

    visuals_snapshot_free(&snapshot);

    if (need_redraw) do_cmd_redraw();
}


/*
 * Interact with "colors"
 */
void do_cmd_colors(void)
{
    int i;

    char tmp[160];

    char buf[1024];


    /* File type is "TEXT" */
    FILE_TYPE(FILE_TYPE_TEXT);


    /* Save the screen */
    screen_save();


    /* Interact until done */
    while (1)
    {
        /* Clear screen */
        Term_clear();

        /* Ask for a choice */
        prt("Interact with Colors", 2, 0);


        /* Give some choices */
        prt("(1) Load a user pref file", 4, 5);

#ifdef ALLOW_COLORS
        prt("(2) Dump colors", 5, 5);
        prt("(3) Modify colors", 6, 5);
        prt("(4) Load simple color set", 7, 5);
        prt("(5) Load Windows color set", 8, 5);

#endif

        /* Prompt */
        prt("Command: ", 10, 0);


        /* Prompt */
        i = inkey();

        /* Done */
        if (i == ESCAPE) break;

        /* Load a 'pref' file */
        if (i == '1')
        {
            /* Prompt */
            prt("Command: Load a user pref file", 10, 0);


            /* Prompt */
            prt("File: ", 12, 0);


            /* Default file */
            sprintf(tmp, "%s.prf", pref_save_base);

            /* Query */
            if (!askfor(tmp, 70)) continue;

            /* Process the given filename */
            (void)process_pref_file(tmp);

            /* Mega-Hack -- react to changes */
            Term_xtra(TERM_XTRA_REACT, 0);

            /* Mega-Hack -- redraw */
            Term_redraw();
        }

#ifdef ALLOW_COLORS

        /* Dump colors */
        else if (i == '2')
        {
            static cptr mark = "Colors";

            /* Prompt */
            prt("Command: Dump colors", 10, 0);


            /* Prompt */
            prt("File: ", 12, 0);


            /* Default filename */
            sprintf(tmp, "%s.prf", pref_save_base);

            /* Get a filename */
            if (!askfor(tmp, 70)) continue;

            /* Build the filename */
            path_build(buf, sizeof(buf), ANGBAND_DIR_USER, tmp);

            /* Append to the file */
            if (!open_auto_dump(buf, mark)) continue;

            /* Start dumping */
            auto_dump_printf("\n# Color redefinitions\n\n");

            /* Dump colors */
            for (i = 0; i < 256; i++)
            {
                int kv = angband_color_table[i][0];
                int rv = angband_color_table[i][1];
                int gv = angband_color_table[i][2];
                int bv = angband_color_table[i][3];

                cptr name = "unknown";


                /* Skip non-entries */
                if (!kv && !rv && !gv && !bv) continue;

                /* Extract the color name */
                if (i < MAX_COLOR) name = color_names[i];

                /* Dump a comment */
                auto_dump_printf("# Color '%s'\n", name);

                /* Dump the monster attr/char info */
                auto_dump_printf("V:%d:0x%02X:0x%02X:0x%02X:0x%02X\n\n",
                    i, kv, rv, gv, bv);
            }

            /* Close */
            close_auto_dump();

            /* Message */
            msg_print("Dumped color redefinitions.");

        }

        /* Edit colors */
        else if (i == '3')
        {
            static byte a = 0;

            /* Prompt */
            prt("Command: Modify colors", 10, 0);


            /* Hack -- query until done */
            while (1)
            {
                cptr name;
                byte j;

                /* Clear */
                clear_from(10);

                /* Exhibit the normal colors */
                for (j = 0; j < 16; j++)
                {
                    /* Exhibit this color */
                    Term_putstr(j*4, 19, -1, a, "###");

                    /* Exhibit all colors */
                    Term_putstr(j*4, 20, -1, j, format("%3d", j));
                }
                if (MAX_COLOR > 16)
                {
                    for (j = 0; j < MAX_COLOR - 16; j++)
                    {
                        /* Exhibit this color */
                        Term_putstr(j*4, 21, -1, a, "###");

                        /* Exhibit all colors */
                        Term_putstr(j*4, 22, -1, j + 16, format("%3d", j + 16));
                    }
                }

                /* Describe the color */
                name = ((a < MAX_COLOR) ? color_names[a] : "undefined");


                /* Describe the color */
                Term_putstr(5, 12, -1, TERM_WHITE,
                        format("Color = %d, Name = %s", a, name));


                /* Label the Current values */
                Term_putstr(5, 14, -1, TERM_WHITE,
                        format("K = 0x%02x / R,G,B = 0x%02x,0x%02x,0x%02x",
                           angband_color_table[a][0],
                           angband_color_table[a][1],
                           angband_color_table[a][2],
                           angband_color_table[a][3]));

                /* Prompt */
                Term_putstr(0, 16, -1, TERM_WHITE,
                        "Command (n/N/k/K/r/R/g/G/b/B): ");


                /* Get a command */
                i = inkey();

                /* All done */
                if (i == ESCAPE) break;

                /* Analyze */
                if (i == 'n') a = (byte)(a + 1);
                if (i == 'N') a = (byte)(a - 1);
                if (i == 'k') angband_color_table[a][0] = (byte)(angband_color_table[a][0] + 1);
                if (i == 'K') angband_color_table[a][0] = (byte)(angband_color_table[a][0] - 1);
                if (i == 'r') angband_color_table[a][1] = (byte)(angband_color_table[a][1] + 1);
                if (i == 'R') angband_color_table[a][1] = (byte)(angband_color_table[a][1] - 1);
                if (i == 'g') angband_color_table[a][2] = (byte)(angband_color_table[a][2] + 1);
                if (i == 'G') angband_color_table[a][2] = (byte)(angband_color_table[a][2] - 1);
                if (i == 'b') angband_color_table[a][3] = (byte)(angband_color_table[a][3] + 1);
                if (i == 'B') angband_color_table[a][3] = (byte)(angband_color_table[a][3] - 1);

                /* Hack -- react to changes */
                Term_xtra(TERM_XTRA_REACT, 0);

                /* Hack -- redraw */
                Term_redraw();
            }
        }

        else if (i == '4')
        {
            if (process_pref_file("user-lim.prf")) msg_print("Done.");
            Term_xtra(TERM_XTRA_REACT, 0);
            Term_redraw();
        }

        else if (i == '5')
        {
            if (process_pref_file("user-win.prf")) msg_print("Done.");
            Term_xtra(TERM_XTRA_REACT, 0);
            Term_redraw();
        }

#endif

        /* Unknown option */
        else
        {
            bell();
        }

        /* Flush messages */
        msg_print(NULL);
    }


    /* Restore the screen */
    screen_load();
}

void msg_add_tiny_screenshot(int cx, int cy)
{
    if (!statistics_hack)
    {
        string_ptr s = get_tiny_screenshot(cx, cy);
        msg_add(string_buffer(s));
        string_free(s);
    }
}

string_ptr get_tiny_screenshot(int cx, int cy)
{
    string_ptr s = string_alloc_size(cx * cy);
    bool       old_use_graphics = use_graphics;
    int        y1, y2, x1, x2, y, x;

    y1 = py - cy/2;
    y2 = py + cy/2;
    if (y1 < 0) y1 = 0;
    if (y2 > cur_hgt) y2 = cur_hgt;

    x1 = px - cx/2;
    x2 = px + cx/2;
    if (x1 < 0) x1 = 0;
    if (x2 > cur_wid) x2 = cur_wid;

    if (old_use_graphics)
    {
        use_graphics = FALSE;
        reset_visuals();
    }

    for (y = y1; y < y2; y++)
    {
        int  current_a = -1;
        for (x = x1; x < x2; x++)
        {
            byte a, ta;
            char c, tc;

            assert(in_bounds2(y, x));
            map_info(y, x, &a, &c, &ta, &tc);

            if (c == 127) /* Hack for special wall characters on Windows. See font-win.prf and main-win.c */
                c = '#';

            if (a != current_a)
            {
                if (current_a >= 0 && current_a != TERM_WHITE)
                {
                    string_append_s(s, "</color>");
                }
                if (a != TERM_WHITE)
                {
                    string_printf(s, "<color:%c>", attr_to_attr_char(a));
                }
                current_a = a;
            }
            string_append_c(s, c);
        }
        if (current_a >= 0 && current_a != TERM_WHITE)
            string_append_s(s, "</color>");
        string_append_c(s, '\n');
    }
    if (old_use_graphics)
    {
        use_graphics = TRUE;
        reset_visuals();
    }
    return s;
}

/* Note: This will not work if the screen is "icky" */
string_ptr get_screenshot(void)
{
    string_ptr s = string_alloc_size(80 * 27);
    bool       old_use_graphics = use_graphics;
    int        wid, hgt, x, y;

    Term_get_size(&wid, &hgt);

    if (old_use_graphics)
    {
        use_graphics = FALSE;
        reset_visuals();

        p_ptr->redraw |= (PR_WIPE | PR_BASIC | PR_EXTRA | PR_MAP | PR_EQUIPPY | PR_MSG_LINE);
        redraw_stuff();
    }

    for (y = 0; y < hgt; y++)
    {
        int  current_a = -1;
        for (x = 0; x < wid; x++)
        {
            byte a;
            char c;

            Term_what(x, y, &a, &c);

            if (c == 127) /* Hack for special wall characters on Windows. See font-win.prf and main-win.c */
                c = '#';

            if (a != current_a)
            {
                if (current_a >= 0 && current_a != TERM_WHITE)
                {
                    string_append_s(s, "</color>");
                }
                if (a != TERM_WHITE)
                {
                    string_printf(s, "<color:%c>", attr_to_attr_char(a));
                }
                current_a = a;
            }
            string_append_c(s, c);
        }
        if (current_a >= 0 && current_a != TERM_WHITE)
            string_append_s(s, "</color>");
        string_append_c(s, '\n');
    }
    if (old_use_graphics)
    {
        use_graphics = TRUE;
        reset_visuals();

        p_ptr->redraw |= (PR_WIPE | PR_BASIC | PR_EXTRA | PR_MAP | PR_EQUIPPY | PR_MSG_LINE);
        redraw_stuff();
    }
    return s;
}

/*
 * Note something in the message recall
 */
void do_cmd_note(void)
{
    char buf[80];
    string_ptr s = 0;

    /* Default */
    strcpy(buf, "");

    /* Input */
    if (!get_string("Note: ", buf, 60)) return;

    /* Ignore empty notes */
    if (!buf[0] || (buf[0] == ' ')) return;

    /* Add the note to the message recall */
    msg_format("<color:R>Note:</color> %s\n", buf);

    s = get_tiny_screenshot(50, 24);
    msg_add(string_buffer(s));
    string_free(s);
}


/*
 * Mention the current version
 */
void do_cmd_version(void)
{
    cptr xtra = "";
    if (VER_MINOR == 0)
    {
/*        if (VER_PATCH == 0) xtra = " (Alpha)"; */
        if (VER_MAJOR != 7) xtra = " (Beta)";
    }
    msg_format("You are playing <color:B>FrogComposband</color> <color:r>%d.%d.%s%s</color>.",
        VER_MAJOR, VER_MINOR, VER_PATCH, xtra);
    if (1)
    {
        rect_t r = ui_map_rect();
        msg_format("Map display is %dx%d.", r.cx, r.cy);
    }
}



/*
 * Array of feeling strings
 */
struct _feeling_info_s
{
    byte color;
    cptr msg;
};
typedef struct _feeling_info_s _feeling_info_t;
static _feeling_info_t _level_feelings[11] =
{
    {TERM_SLATE, "Looks like any other level."},
    {TERM_L_BLUE, "You feel there is something special about this level."},
    {TERM_VIOLET, "You nearly faint as horrible visions of death fill your mind!"},
    {TERM_RED, "This level looks very dangerous."},
    {TERM_L_RED, "You have a very bad feeling..."},
    {TERM_ORANGE, "You have a bad feeling..."},
    {TERM_YELLOW, "You feel nervous."},
    {TERM_L_UMBER, "You feel your luck is turning..."},
    {TERM_L_WHITE, "You don't like the look of this place."},
    {TERM_WHITE, "This level looks reasonably safe."},
    {TERM_WHITE, "What a boring place..."},
};

static _feeling_info_t _level_feelings_lucky[11] =
{
    {TERM_SLATE, "Looks like any other level."},
    {TERM_L_BLUE, "You feel there is something special about this level."},
    {TERM_VIOLET, "You have a superb feeling about this level."},
    {TERM_RED, "You have an excellent feeling..."},
    {TERM_L_RED, "You have a very good feeling..."},
    {TERM_ORANGE, "You have a good feeling..."},
    {TERM_YELLOW, "You feel strangely lucky..."},
    {TERM_L_UMBER, "You feel your luck is turning..."},
    {TERM_L_WHITE, "You like the look of this place..."},
    {TERM_WHITE, "This level can't be all bad..."},
    {TERM_WHITE, "What a boring place..."},
};


/*
 * Note that "feeling" is set to zero unless some time has passed.
 * Note that this is done when the level is GENERATED, not entered.
 */
void do_cmd_feeling(void)
{
    /* No useful feeling in quests */
    if (!quests_allow_feeling())
    {
        msg_print("Looks like a typical quest level.");
    }

    /* No useful feeling in town */
    else if (p_ptr->town_num && !dun_level)
    {
        if (!strcmp(town_name(p_ptr->town_num), "Wilderness"))
        {
            msg_print("Looks like a strange wilderness.");
        }
        else
        {
            msg_print("Looks like a typical town.");
        }
    }

    /* No useful feeling in the wilderness */
    else if (!dun_level)
    {
        msg_print("Looks like a typical wilderness.");
    }

    /* Display the feeling */
    else
    {
        _feeling_info_t feeling;
        assert(/*0 <= p_ptr->feeling &&*/ p_ptr->feeling < 11);
        if (p_ptr->good_luck || p_ptr->pclass == CLASS_ARCHAEOLOGIST)
            feeling = _level_feelings_lucky[p_ptr->feeling];
        else
            feeling = _level_feelings[p_ptr->feeling];
        cmsg_print(feeling.color, feeling.msg);
    }
}



/*
 * Description of each monster group.
 */
static cptr monster_group_text[] =
{
    "Corpses",
    "Uniques",
    "Ridable monsters",
    "Wanted monsters",
    "Dungeon guardians",
    "Amberite",
    "God",
    "Ant",
    "Bat",
    "Centipede",
    "Dragon",
    "Floating Eye",
    "Feline/Fox",
    "Golem",
    "Hobbit/Elf/Dwarf",
    "Icky Thing",
    "Jelly",
    "Kobold",
    "Aquatic monster",
    "Mold",
    "Naga",
    "Orc",
    "Person/Human",
    "Quadruped",
    "Rodent",
    "Skeleton",
    "Demon",
    "Vortex",
    "Worm/Worm-Mass",
    /* "unused", */
    "Yeek",
    "Zombie/Mummy",
    "Angel",
    "Bird",
    "Canine",
    /* "Ancient Dragon/Wyrm", */
    "Elemental",
    "Dragon Fly",
    "Ghost",
    "Hybrid",
    "Insect",
    "Snake",
    "Killer Beetle",
    "Lich",
    "Multi-Headed Reptile",
    "Mystery Living",
    "Ogre",
    "Giant Humanoid",
    "Quylthulg",
    "Reptile/Amphibian",
    "Spider/Scorpion/Tick",
    "Troll",
    /* "Major Demon", */
    "Vampire",
    "Wight/Wraith/etc",
    "Xorn/Xaren/etc",
    "Yeti",
    "Zephyr Hound",
    "Mimic",
    "Wall/Plant/Gas",
    "Mushroom patch",
    "Ball",
    "Player",
    NULL
};


/*
 * Symbols of monsters in each group. Note the "Uniques" group
 * is handled differently.
 */
static cptr monster_group_char[] =
{
    (char *) -1L,
    (char *) -2L,
    (char *) -3L,
    (char *) -4L,
    (char *) -5L,
    (char *) -6L,
    (char *) -7L,
    "a",
    "b",
    "c",
    "dD",
    "e",
    "f",
    "g",
    "h",
    "i",
    "j",
    "k",
    "l",
    "m",
    "n",
    "o",
    "pt",
    "q",
    "r",
    "s",
    "uU",
    "v",
    "w",
    /* "x", */
    "y",
    "z",
    "A",
    "B",
    "C",
    /* "D", */
    "E",
    "F",
    "G",
    "H",
    "I",
    "J",
    "K",
    "L",
    "M",
    "N",
    "O",
    "P",
    "Q",
    "R",
    "S",
    "T",
    /* "U", */
    "V",
    "W",
    "X",
    "Y",
    "Z",
    "!$&()+./=>?[\\]`{|~x",
    "#%",
    ",",
    "*",
    "@",
    NULL
};


/*
 * hook function to sort monsters by level
 */
static bool ang_sort_comp_monster_level(vptr u, vptr v, int a, int b)
{
    u16b *who = (u16b*)(u);

    int w1 = who[a];
    int w2 = who[b];

    monster_race *r_ptr1 = &r_info[w1];
    monster_race *r_ptr2 = &r_info[w2];

    /* Unused */
    (void)v;

    if (r_ptr2->level > r_ptr1->level) return FALSE;
    if (r_ptr1->level > r_ptr2->level) return TRUE;

    if ((r_ptr2->flags1 & RF1_UNIQUE) && !(r_ptr1->flags1 & RF1_UNIQUE)) return TRUE;
    if ((r_ptr1->flags1 & RF1_UNIQUE) && !(r_ptr2->flags1 & RF1_UNIQUE)) return FALSE;
    return w1 <= w2;
}

/*
 * Build a list of monster indexes in the given group. Return the number
 * of monsters in the group.
 *
 * mode & 0x01 : check for non-empty group
 * mode & 0x02 : visual operation only
 */
static int collect_monsters(int grp_cur, s16b mon_idx[], byte mode)
{
    int i, mon_cnt = 0;
    int dummy_why;

    /* Get a list of x_char in this group */
    cptr group_char = monster_group_char[grp_cur];

    /* XXX Hack -- Check for special groups */
    bool        grp_corpses = (monster_group_char[grp_cur] == (char *) -1L);
    bool        grp_unique = (monster_group_char[grp_cur] == (char *) -2L);
    bool        grp_riding = (monster_group_char[grp_cur] == (char *) -3L);
    bool        grp_wanted = (monster_group_char[grp_cur] == (char *) -4L);
    bool        grp_guardian = (monster_group_char[grp_cur] == (char *) -5L);
    bool        grp_amberite = (monster_group_char[grp_cur] == (char *) -6L);
    bool        grp_god = (monster_group_char[grp_cur] == (char *) -7L);
    int_map_ptr available_corpses = NULL;

    if (grp_corpses)
    {
        available_corpses = int_map_alloc(NULL);

        /* In Pack */
        for (i = 1; i <= pack_max(); i++)
        {
            object_type *o_ptr = pack_obj(i);
            if (!o_ptr) continue;
            if (!object_is_(o_ptr, TV_CORPSE, SV_CORPSE)) continue;
            int_map_add(available_corpses, o_ptr->pval, NULL);
        }

        /* At Home */
        for (i = 1; i <= home_max(); i++)
        {
            object_type *o_ptr = home_obj(i);
            if (!o_ptr) continue;
            if (!object_is_(o_ptr, TV_CORPSE, SV_CORPSE)) continue;
            int_map_add(available_corpses, o_ptr->pval, NULL);
        }

        /* Underfoot */
        if (in_bounds2(py, px))
        {
            cave_type  *c_ptr = &cave[py][px];
            s16b        o_idx = c_ptr->o_idx;

            while (o_idx)
            {
                object_type *o_ptr = &o_list[o_idx];

                if (object_is_(o_ptr, TV_CORPSE, SV_CORPSE))
                    int_map_add(available_corpses, o_ptr->pval, NULL);

                o_idx = o_ptr->next_o_idx;
            }
        }

        /* Current Form for Easier Comparisons */
        if (p_ptr->prace == RACE_MON_POSSESSOR && p_ptr->current_r_idx != MON_POSSESSOR_SOUL)
            int_map_add(available_corpses, p_ptr->current_r_idx, NULL);

    }


    /* Check every race */
    for (i = 1; i < max_r_idx; i++)
    {
        /* Access the race */
        monster_race *r_ptr = &r_info[i];

        /* Skip empty race */
        if (!r_ptr->name) continue;
        if (!p_ptr->wizard && (r_ptr->flagsx & RFX_SUPPRESS)) continue;

        /* Require known monsters */
        if (!(mode & 0x02) && !easy_lore && !r_ptr->r_sights) continue;

        if (grp_corpses)
        {
            if (!int_map_contains(available_corpses, i))
                continue;
        }

        else if (grp_unique)
        {
            if (!(r_ptr->flags1 & RF1_UNIQUE)) continue;
        }

        else if (grp_riding)
        {
            if (!(r_ptr->flags7 & RF7_RIDING)) continue;
        }

        else if (grp_wanted)
        {
            bool wanted = FALSE;
            int j;
            for (j = 0; j < MAX_KUBI; j++)
            {
                if (kubi_r_idx[j] == i || kubi_r_idx[j] - 10000 == i ||
                    (p_ptr->today_mon && p_ptr->today_mon == i))
                {
                    wanted = TRUE;
                    break;
                }
            }
            if (!wanted) continue;
        }

        else if (grp_amberite)
        {
            if (!(r_ptr->flags3 & RF3_AMBERITE)) continue;
        }

        else if (grp_god)
        {
            if (!(r_ptr->flags1 & RF1_UNIQUE)) continue;
            if (!monster_pantheon(r_ptr)) continue;
        }

        else if (grp_guardian)
        {
            if (!(r_ptr->flags7 & RF7_GUARDIAN)) continue;
            if ((d_info[DUNGEON_MYSTERY].final_guardian == i) &&
                (!(d_info[DUNGEON_MYSTERY].flags1 & DF1_SUPPRESSED)) &&
                (d_info[DUNGEON_MYSTERY].maxdepth > max_dlv[DUNGEON_MYSTERY])) continue;
        }

        else
        {
            /* Check for race in the group */
            if (!my_strchr(group_char, r_ptr->d_char)) continue;
        }

        /* Add the race */
        mon_idx[mon_cnt++] = i;

        /* XXX Hack -- Just checking for non-empty group */
        if (mode & 0x01) break;
    }

    /* Terminate the list */
    mon_idx[mon_cnt] = -1;

    /* Select the sort method */
    ang_sort_comp = ang_sort_comp_monster_level;
    ang_sort_swap = ang_sort_swap_hook;

    /* Sort by monster level */
    ang_sort(mon_idx, &dummy_why, mon_cnt);

    if (grp_corpses)
        int_map_free(available_corpses);

    /* Return the number of races */
    return mon_cnt;
}


/*
 * Description of each object group.
 */
static cptr object_group_text[] =
{
    "Food",
    "Potions",
/*  "Flasks", */
    "Scrolls",
/*  "Rings",
    "Amulets", */
/*  "Whistle",
    "Lanterns", */
/*  "Wands",
    "Staves",
    "Rods", */
/*  "Cards",
    "Capture Balls",
    "Parchments",
    "Spikes",
    "Boxs",
    "Figurines",
    "Statues",
    "Junks",
    "Bottles",
    "Skeletons",
    "Corpses", */
    "Swords",
    "Blunt Weapons",
    "Polearms",
    "Diggers",
    "Bows",
    "Shots",
    "Arrows",
    "Bolts",
    "Soft Armor",
    "Hard Armor",
    "Dragon Armor",
    "Shields",
    "Cloaks",
    "Gloves",
    "Helms",
    "Crowns",
    "Boots",
    "Spellbooks",
/*  "Treasure", */
    "Something",
    NULL
};


/*
 * TVALs of items in each group
 */
static byte object_group_tval[] =
{
    TV_FOOD,
    TV_POTION,
/*  TV_FLASK, */
    TV_SCROLL,
/*  TV_RING,
    TV_AMULET, */
/*  TV_WHISTLE,
    TV_LITE, */
/*  TV_WAND,
    TV_STAFF,
    TV_ROD,  */
/*  TV_CARD,
    TV_CAPTURE,
    TV_PARCHMENT,
    TV_SPIKE,
    TV_CHEST,
    TV_FIGURINE,
    TV_STATUE,
    TV_JUNK,
    TV_BOTTLE,
    TV_SKELETON,
    TV_CORPSE, */
    TV_SWORD,
    TV_HAFTED,
    TV_POLEARM,
    TV_DIGGING,
    TV_BOW,
    TV_SHOT,
    TV_ARROW,
    TV_BOLT,
    TV_SOFT_ARMOR,
    TV_HARD_ARMOR,
    TV_DRAG_ARMOR,
    TV_SHIELD,
    TV_CLOAK,
    TV_GLOVES,
    TV_HELM,
    TV_CROWN,
    TV_BOOTS,
    TV_LIFE_BOOK, /* Hack -- all spellbooks */
/*  TV_GOLD, */
    0,
    0,
};

static bool _compare_k_level(vptr u, vptr v, int a, int b)
{
    int *indices = (int*)u;
    int left = indices[a];
    int right = indices[b];
    return k_info[left].level <= k_info[right].level;
}

static void _swap_int(vptr u, vptr v, int a, int b)
{
    int *indices = (int*)u;
    int tmp = indices[a];
    indices[a] = indices[b];
    indices[b] = tmp;
}

/*
 * Build a list of object indexes in the given group. Return the number
 * of objects in the group.
 *
 * mode & 0x01 : check for non-empty group
 * mode & 0x02 : visual operation only
 */
static int collect_objects(int grp_cur, int object_idx[], byte mode)
{
    int i, j, k, object_cnt = 0;

    /* Get a list of x_char in this group */
    byte group_tval = object_group_tval[grp_cur];

    /* Check every object */
    for (i = 0; i < max_k_idx; i++)
    {
        /* Access the object */
        object_kind *k_ptr = &k_info[i];

        /* Skip empty objects */
        if (!k_ptr->name) continue;

        if (mode & 0x02)
        {
            /* Any objects will be displayed */
        }
        else
        {
            if (!k_ptr->flavor)
            {
                if (!k_ptr->counts.found && !k_ptr->counts.bought) continue;
            }

            /* Require objects ever seen */
            if (!k_ptr->aware) continue;

            /* Skip items with no distribution (special artifacts) */
            for (j = 0, k = 0; j < 4; j++) k += k_ptr->chance[j];
            if (!k) continue;
        }

        /* Check for objects in the group */
        if (TV_LIFE_BOOK == group_tval)
        {
            /* Hack -- All spell books */
            if (TV_BOOK_BEGIN <= k_ptr->tval && k_ptr->tval <= TV_BOOK_END)
            {
                /* Add the object */
                object_idx[object_cnt++] = i;
            }
            else continue;
        }
        else if (k_ptr->tval == group_tval)
        {
            /* Add the object */
            object_idx[object_cnt++] = i;
        }
        else continue;

        /* XXX Hack -- Just checking for non-empty group */
        if (mode & 0x01) break;
    }

    /* Sort Results */
    ang_sort_comp = _compare_k_level;
    ang_sort_swap = _swap_int;
    ang_sort(object_idx, NULL, object_cnt);

    /* Terminate the list */
    object_idx[object_cnt] = -1;

    /* Return the number of objects */
    return object_cnt;
}


/*
 * Description of each feature group.
 */
static cptr feature_group_text[] =
{
    "terrains",
    NULL
};


/*
 * Build a list of feature indexes in the given group. Return the number
 * of features in the group.
 *
 * mode & 0x01 : check for non-empty group
 */
static int collect_features(int grp_cur, int *feat_idx, byte mode)
{
    int i, feat_cnt = 0;

    /* Unused;  There is a single group. */
    (void)grp_cur;

    /* Check every feature */
    for (i = 0; i < max_f_idx; i++)
    {
        /* Access the index */
        feature_type *f_ptr = &f_info[i];

        /* Skip empty index */
        if (!f_ptr->name) continue;

        /* Skip mimiccing features */
        if (f_ptr->mimic != i) continue;

        /* Add the index */
        feat_idx[feat_cnt++] = i;

        /* XXX Hack -- Just checking for non-empty group */
        if (mode & 0x01) break;
    }

    /* Terminate the list */
    feat_idx[feat_cnt] = -1;

    /* Return the number of races */
    return feat_cnt;
}

void do_cmd_save_screen_doc(void)
{
    string_ptr s = get_screenshot();
    char       buf[1024];
    FILE      *fff;

    path_build(buf, sizeof(buf), ANGBAND_DIR_USER, "screen.doc");
    FILE_TYPE(FILE_TYPE_TEXT);
    fff = my_fopen(buf, "w");
    if (fff)
    {
        string_write_file(s, fff);
        my_fclose(fff);
    }
    string_free(s);
}

void save_screen_aux(cptr file, int format)
{
    string_ptr s = get_screenshot();
    doc_ptr    doc = doc_alloc(Term->wid);
    FILE      *fff;

    doc_insert(doc, "<style:screenshot>");
    doc_insert(doc, string_buffer(s));
    doc_insert(doc, "</style>");

    FILE_TYPE(FILE_TYPE_TEXT);
    fff = my_fopen(file, "w");
    if (fff)
    {
        doc_write_file(doc, fff, format);
        my_fclose(fff);
    }
    string_free(s);
    doc_free(doc);
}

static void _save_screen_aux(int format)
{
    char buf[1024];

    if (format == DOC_FORMAT_HTML)
        path_build(buf, sizeof(buf), ANGBAND_DIR_USER, "screen.html");
    else
        path_build(buf, sizeof(buf), ANGBAND_DIR_USER, "screen.txt");

    save_screen_aux(buf, format);
}

void do_cmd_save_screen_txt(void)
{
    _save_screen_aux(DOC_FORMAT_TEXT);
}

void do_cmd_save_screen_html(void)
{
    _save_screen_aux(DOC_FORMAT_HTML);
}

void do_cmd_save_screen(void)
{
    string_ptr s = get_screenshot();
    doc_ptr    doc = doc_alloc(Term->wid);

    doc_insert(doc, "<style:screenshot>");
    doc_insert(doc, string_buffer(s));
    doc_insert(doc, "</style>");
    screen_save();
    doc_display(doc, "Current Screenshot", 0);
    screen_load();

    string_free(s);
    doc_free(doc);
}

/************************************************************************
 * Artifact Lore (Standard Arts Only)
 * Note: Check out the Wizard Spoiler Commands for an alternative approach.
 *       ^a"a and ^a"O
 ************************************************************************/
typedef struct {
    object_p filter;
    cptr     name;
} _art_type_t;

static _art_type_t _art_types[] = {
    { object_is_melee_weapon, "Weapons" },
    { object_is_shield, "Shield" },
    { object_is_bow, "Bows" },
    { object_is_ring, "Rings" },
    { object_is_amulet, "Amulets" },
    { object_is_lite, "Lights" },
    { object_is_body_armour, "Body Armor" },
    { object_is_cloak, "Cloaks" },
    { object_is_helmet, "Helmets" },
    { object_is_gloves, "Gloves" },
    { object_is_boots, "Boots" },
    { object_is_ammo, "Ammo" },
    { NULL, NULL },
};

static bool _compare_a_level(vptr u, vptr v, int a, int b)
{
    int *indices = (int*)u;
    int left = indices[a];
    int right = indices[b];
    return a_info[left].level <= a_info[right].level;
}

static int _collect_arts(int grp_cur, int art_idx[], bool show_all)
{
    int i, cnt = 0;

    for (i = 0; i < max_a_idx; i++)
    {
        artifact_type *a_ptr = &a_info[i];
        object_type    forge;

        if (!a_ptr->name) continue;
        if (!a_ptr->found)
        {
            if (!show_all) continue;
            /*if (!a_ptr->generated) continue;*/
            if (!art_has_lore(a_ptr)) continue;
        }
        if (!create_named_art_aux_aux(i, &forge)) continue;
        if (!_art_types[grp_cur].filter(&forge)) continue;

        art_idx[cnt++] = i;
    }

    /* Sort Results */
    ang_sort_comp = _compare_a_level;
    ang_sort_swap = _swap_int;
    ang_sort(art_idx, NULL, cnt);

    /* Terminate the list */
    art_idx[cnt] = -1;

    return cnt;
}


static void do_cmd_knowledge_artifacts(void)
{
    static bool show_all = TRUE;

    int i, len, max;
    int grp_cur, grp_top, old_grp_cur;
    int art_cur, art_top;
    int grp_cnt, grp_idx[100];
    int art_cnt;
    int *art_idx;

    int column = 0;
    bool flag;
    bool redraw;
    bool rebuild;

    int browser_rows;
    int wid, hgt;

    if (random_artifacts)
    {
        /* FIXED_ART ... 
        if (random_artifact_pct >= 100)
        {
            cmsg_print(TERM_L_RED, "You won't find any fixed artifacts this game.");
            return;
        }
        */
    }
    else if (no_artifacts)
    {
        cmsg_print(TERM_L_RED, "You won't find any artifacts this game.");
        return;
    }

    /* Get size */
    Term_get_size(&wid, &hgt);

    browser_rows = hgt - 8;

    C_MAKE(art_idx, max_a_idx, int);

    max = 0;
    grp_cnt = 0;
    for (i = 0; _art_types[i].filter; i++)
    {
        len = strlen(_art_types[i].name);
        if (len > max)
            max = len;

        if (_collect_arts(i, art_idx, TRUE))
            grp_idx[grp_cnt++] = i;
    }
    grp_idx[grp_cnt] = -1;

    if (!grp_cnt)
    {
        prt("You haven't found any artifacts just yet. Press any key to continue.", 0, 0);
        inkey();
        prt("", 0, 0);
        C_KILL(art_idx, max_a_idx, int);
        return;
    }

    art_cnt = 0;

    old_grp_cur = -1;
    grp_cur = grp_top = 0;
    art_cur = art_top = 0;

    flag = FALSE;
    redraw = TRUE;
    rebuild = TRUE;

    while (!flag)
    {
        char ch;
        if (redraw)
        {
            clear_from(0);

            prt(format("%s - Artifacts", "Knowledge"), 2, 0);
            prt("Group", 4, 0);
            prt("Name", 4, max + 3);

            for (i = 0; i < 72; i++)
            {
                Term_putch(i, 5, TERM_WHITE, '=');
            }

            for (i = 0; i < browser_rows; i++)
            {
                Term_putch(max + 1, 6 + i, TERM_WHITE, '|');
            }

            redraw = FALSE;
        }

        /* Scroll group list */
        if (grp_cur < grp_top) grp_top = grp_cur;
        if (grp_cur >= grp_top + browser_rows) grp_top = grp_cur - browser_rows + 1;

        /* Display a list of object groups */
        for (i = 0; i < browser_rows && grp_idx[i] >= 0; i++)
        {
            int  grp = grp_idx[grp_top + i];
            byte attr = (grp_top + i == grp_cur) ? TERM_L_BLUE : TERM_WHITE;

            Term_erase(0, 6 + i, max);
            c_put_str(attr, _art_types[grp].name, 6 + i, 0);
        }

        if (rebuild || old_grp_cur != grp_cur)
        {
            old_grp_cur = grp_cur;

            /* Get a list of objects in the current group */
            art_cnt = _collect_arts(grp_idx[grp_cur], art_idx, show_all);
            rebuild = FALSE;
        }

        /* Scroll object list */
        while (art_cur < art_top)
            art_top = MAX(0, art_top - browser_rows/2);
        while (art_cur >= art_top + browser_rows)
            art_top = MIN(art_cnt - browser_rows, art_top + browser_rows/2);

        /* Display a list of objects in the current group */
        /* Display lines until done */
        for (i = 0; i < browser_rows && art_top + i < art_cnt && art_idx[art_top + i] >= 0; i++)
        {
            char        name[MAX_NLEN];
            int         idx = art_idx[art_top + i];
            object_type forge;
            byte        attr = TERM_WHITE;

            create_named_art_aux_aux(idx, &forge);
            forge.ident = IDENT_KNOWN;
            object_desc(name, &forge, OD_OMIT_INSCRIPTION);

            if (i + art_top == art_cur)
                attr = TERM_L_BLUE;
            else if ((p_ptr->wizard) &&(!a_info[idx].generated))
                attr = TERM_L_DARK;
            else if (!a_info[idx].found)
                attr = (p_ptr->wizard) ? TERM_GREEN : TERM_L_DARK;
            else
                attr = tval_to_attr[forge.tval % 128];

            c_prt(attr, name, 6 + i, max + 3);
        }

        /* Clear remaining lines */
        for (; i < browser_rows; i++)
        {
            Term_erase(max + 3, 6 + i, 255);
        }

        if (show_all)
            prt("<dir>, 'r' to recall, 't' to Hide Unfound, ESC", hgt - 1, 0);
        else
            prt("<dir>, 'r' to recall, 't' to Show All, ESC", hgt - 1, 0);

        if (!column)
        {
            Term_gotoxy(0, 6 + (grp_cur - grp_top));
        }
        else
        {
            Term_gotoxy(max + 3, 6 + (art_cur - art_top));
        }

        ch = inkey();

        switch (ch)
        {
        case ESCAPE:
            flag = TRUE;
            break;

        case 'T': case 't':
            show_all = !show_all;
            art_cur = 0;
            rebuild = TRUE;
            break;

        case 'R': case 'r':
        case 'I': case 'i':
            if (grp_cnt > 0 && art_idx[art_cur] >= 0)
            {
                int idx = art_idx[art_cur];
                object_type forge;
                create_named_art_aux_aux(idx, &forge);
                forge.ident = IDENT_KNOWN;
                obj_display(&forge);
                redraw = TRUE;
            }
            break;

        default:
            browser_cursor(ch, &column, &grp_cur, grp_cnt, &art_cur, art_cnt);
        }
    }

    C_KILL(art_idx, max_a_idx, int);
}


/*
 * Display known uniques
 * With "XTRA HACK UNIQHIST" (Originally from XAngband)
 */
static void do_cmd_knowledge_uniques(void)
{
    int i, k, n = 0;
    u16b why = 2;
    s16b *who;

    FILE *fff;

    char file_name[1024];

    int n_alive[10];
    int n_alive_surface = 0;
    int n_alive_over100 = 0;
    int n_alive_total = 0;
    int max_lev = -1;

    for (i = 0; i < 10; i++) n_alive[i] = 0;

    /* Open a new file */
    fff = my_fopen_temp(file_name, 1024);

    if (!fff)
    {
        msg_format("Failed to create temporary file %s.", file_name);
        msg_print(NULL);
        return;
    }

    /* Allocate the "who" array */
    C_MAKE(who, max_r_idx, s16b);

    /* Scan the monsters */
    for (i = 1; i < max_r_idx; i++)
    {
        monster_race *r_ptr = &r_info[i];
        int          lev;

        if (!r_ptr->name) continue;

        /* Require unique monsters */
        if (!(r_ptr->flags1 & RF1_UNIQUE)) continue;
        if (r_ptr->flagsx & RFX_SUPPRESS) continue;

        /* Only display "known" uniques */
		if (!easy_lore && !r_ptr->r_sights) continue;

        /* Only print rarity <= 100 uniques */
        if (!r_ptr->rarity || ((r_ptr->rarity > 100) && !(r_ptr->flagsx & RFX_QUESTOR))) continue;

        /* Only "alive" uniques */
        if (r_ptr->max_num == 0) continue;

        if (r_ptr->level)
        {
            lev = (r_ptr->level - 1) / 10;
            if (lev < 10)
            {
                n_alive[lev]++;
                if (max_lev < lev) max_lev = lev;
            }
            else n_alive_over100++;
        }
        else n_alive_surface++;

        /* Collect "appropriate" monsters */
        who[n++] = i;
    }

    /* Select the sort method */
    ang_sort_comp = ang_sort_comp_hook;
    ang_sort_swap = ang_sort_swap_hook;

    /* Sort the array by dungeon depth of monsters */
    ang_sort(who, &why, n);

    if (n_alive_surface)
    {
        fprintf(fff, "      Surface  alive: %3d\n", n_alive_surface);
        n_alive_total += n_alive_surface;
    }
    for (i = 0; i <= max_lev; i++)
    {
        fprintf(fff, "Level %3d-%3d  alive: %3d\n", 1 + i * 10, 10 + i * 10, n_alive[i]);
        n_alive_total += n_alive[i];
    }
    if (n_alive_over100)
    {
        fprintf(fff, "Level 101-     alive: %3d\n", n_alive_over100);
        n_alive_total += n_alive_over100;
    }

    if (n_alive_total)
    {
        fputs("-------------  ----------\n", fff);
        fprintf(fff, "        Total  alive: %3d\n\n", n_alive_total);
    }
    else
    {
        fputs("No known uniques alive.\n", fff);
    }

    /* Scan the monster races */
    for (k = 0; k < n; k++)
    {
        monster_race *r_ptr = &r_info[who[k]];

        /* Print a message */
        fprintf(fff, "     %s (level %d)\n", r_name + r_ptr->name, r_ptr->level);
    }

    /* Free the "who" array */
    C_KILL(who, max_r_idx, s16b);

    /* Close the file */
    my_fclose(fff);

    /* Display the file contents */
    show_file(TRUE, file_name, "Alive Uniques", 0, 0);


    /* Remove the file */
    fd_kill(file_name);
}

void do_cmd_knowledge_shooter(void)
{
    doc_ptr doc = doc_alloc(80);

    display_shooter_info(doc);
    if (doc_line_count(doc))
    {
        screen_save();
        doc_display(doc, "Shooting", 0);
        screen_load();
    }
    else
        msg_print("You are not wielding a bow.");

    doc_free(doc);
}

void do_cmd_knowledge_weapon(void)
{
    int i;
    doc_ptr doc = doc_alloc(80);

    for (i = 0; i < MAX_HANDS; i++)
    {
        if (p_ptr->weapon_info[i].wield_how == WIELD_NONE) continue;

        if (p_ptr->weapon_info[i].bare_hands)
            monk_display_attack_info(doc, i);
        else
            display_weapon_info(doc, i);
    }

    for (i = 0; i < p_ptr->innate_attack_ct; i++)
    {
        display_innate_attack_info(doc, i);
    }

    if (doc_line_count(doc))
    {
        screen_save();
        doc_display(doc, "Melee", 0);
        screen_load();
    }
    else
        msg_print("You have no melee attacks.");

    doc_free(doc);
}

void display_weapon_info_aux(int mode)
{
    bool screen_hack = screen_is_saved();
    if (screen_hack) screen_load();

    display_weapon_mode = mode;
    do_cmd_knowledge_weapon();
    display_weapon_mode = 0;

    if (screen_hack) screen_save();
}

static void do_cmd_knowledge_extra(void)
{
    doc_ptr  doc = doc_alloc(80);
    class_t *class_ptr = get_class();
    race_t  *race_ptr = get_race();

    doc_insert(doc, "<style:wide>");

    if (race_ptr->character_dump)
        race_ptr->character_dump(doc);

    if (class_ptr->character_dump)
        class_ptr->character_dump(doc);

    doc_insert(doc, "</style>");

    doc_display(doc, "Race/Class Extra Information", 0);
    doc_free(doc);
}

/*
 * Display weapon-exp.
 */
static int _compare_k_lvl(object_kind *left, object_kind *right)
{
    if (left->level < right->level) return -1;
    if (left->level > right->level) return 1;
    return 0;
}

static vec_ptr _prof_weapon_alloc(int tval)
{
    int i;
    vec_ptr v = vec_alloc(NULL);
    for (i = 0; i < max_k_idx; i++)
    {
        object_kind *k_ptr = &k_info[i];
        if (k_ptr->tval != tval) continue;
        if ((tval == TV_POLEARM) && (k_ptr->sval == (prace_is_(RACE_MON_SWORD) ? SV_DEATH_SCYTHE : SV_DEATH_SCYTHE_HACK))) continue;
        if (tval == TV_BOW && k_ptr->sval == SV_HARP) continue;
        if (tval == TV_BOW && k_ptr->sval == SV_FLUTE) continue;
        if (tval == TV_BOW && k_ptr->sval == SV_CRIMSON) continue;
        if (tval == TV_BOW && k_ptr->sval == SV_RAILGUN) continue;
        vec_add(v, k_ptr);
    }
    vec_sort(v, (vec_cmp_f)_compare_k_lvl);
    return v;
}
 
static cptr _prof_exp_str[5]   = {"[Un]", "[Be]", "[Sk]", "[Ex]", "[Ma]"};
static char _prof_exp_color[5] = {'w',    'G',    'y',    'r',    'v'};
static cptr _prof_weapon_heading(int tval)
{
    switch (tval)
    {
    case TV_SWORD: return "Swords";
    case TV_POLEARM: return "Polearms";
    case TV_HAFTED: return "Hafted";
    case TV_DIGGING: return "Diggers";
    case TV_BOW: return "Bows";
    }
    return "";
}

static void _prof_weapon_doc(doc_ptr doc, int tval, int mode)
{
    vec_ptr v = _prof_weapon_alloc(tval);
    int     i;

    doc_insert_text(doc, TERM_RED, _prof_weapon_heading(tval));
    doc_newline(doc);

    for (i = 0; i < vec_length(v); i++)
    {
        object_kind *k_ptr = vec_get(v, i);
        int          exp = skills_weapon_current(k_ptr->tval, k_ptr->sval);
        int          max = skills_weapon_max(k_ptr->tval, k_ptr->sval);
        int          max_lvl = weapon_exp_level(max);
        int          exp_lvl = weapon_exp_level(exp);
        char         name[MAX_NLEN];

        strip_name(name, k_ptr->idx);
        doc_printf(doc, "<color:%c>%-19s</color> ", equip_find_obj(k_ptr->tval, k_ptr->sval) ? 'B' : 'w', name);
        switch (mode)
        {
            case 1:
                doc_printf(doc, " <color:%c>%-4s</color>", _prof_exp_color[max_lvl], _prof_exp_str[max_lvl]);
                break;
            case 2:
                {
                    s32b pct = 0;
                    int pct_lvl;
                    if (max > 0) pct = ((s32b)exp * 100L) / (s32b)max;
                    pct_lvl = weapon_exp_level((WEAPON_EXP_MASTER / 100) * pct);
                    doc_printf(doc, " <color:%c>%3d%%</color>", _prof_exp_color[pct_lvl], pct);
                    break;
                }
            case 3:
                {
                    s32b pct = ((s32b)exp * 100L) / WEAPON_EXP_MASTER;
                    int pct_lvl = weapon_exp_level((WEAPON_EXP_MASTER / 100) * pct);
                    doc_printf(doc, " <color:%c>%3d%%</color>", _prof_exp_color[pct_lvl], pct);
                    break;
                }
            default:
                doc_printf(doc, "%c<color:%c>%-4s</color>", exp >= max ? '!' : ' ', _prof_exp_color[exp_lvl], _prof_exp_str[exp_lvl]);
                break;
        }
        doc_newline(doc);
    }
    doc_newline(doc);
    vec_free(v);
}

static void _prof_skill_aux(doc_ptr doc, int skill, int mode)
{
    int  exp, max, exp_lvl, max_lvl, pct_lvl;
    cptr name;
    char color = 'w';

    switch (skill)
    {
    case SKILL_MARTIAL_ARTS:
        name = "Martial Arts";
        exp = skills_martial_arts_current();
        max = skills_martial_arts_max();
        max_lvl = weapon_exp_level(max);
        exp_lvl = weapon_exp_level(exp);
        break;
    case SKILL_DUAL_WIELDING:
        name = "Dual Wielding";
        exp = skills_dual_wielding_current();
        max = skills_dual_wielding_max();
        max_lvl = weapon_exp_level(max);
        exp_lvl = weapon_exp_level(exp);
        break;
    case SKILL_RIDING:
    default: /* gcc warnings ... */
        name = "Riding";
        exp = skills_riding_current();
        max = skills_riding_max();
        max_lvl = riding_exp_level(max);
        exp_lvl = riding_exp_level(exp);
        break;
    }
    doc_printf(doc, "<color:%c>%-19s</color> ", color, name);
    switch (mode)
    {
        case 1:
            doc_printf(doc, " <color:%c>%-4s</color>", _prof_exp_color[max_lvl], _prof_exp_str[max_lvl]);
            break;
        case 2:
            {
                s32b pct = 0;
                if (max > 0) pct = ((s32b)exp * 100L) / (s32b)max;
                if (skill == SKILL_RIDING) pct_lvl = riding_exp_level(RIDING_EXP_MASTER / 100 * pct);
                else pct_lvl = weapon_exp_level((WEAPON_EXP_MASTER / 100) * pct);
                doc_printf(doc, " <color:%c>%3d%%</color>", _prof_exp_color[pct_lvl], pct);
                break;
            }
        case 3:
            {
                s32b pct = ((s32b)exp * 100L) / WEAPON_EXP_MASTER;
                if (skill == SKILL_RIDING) pct_lvl = riding_exp_level(RIDING_EXP_MASTER / 100 * pct);
                else pct_lvl = weapon_exp_level((WEAPON_EXP_MASTER / 100) * pct);
                doc_printf(doc, " <color:%c>%3d%%</color>", _prof_exp_color[pct_lvl], pct);
                break;
            }
        default:
            doc_printf(doc, "%c<color:%c>%-4s</color>", exp >= max ? '!' : ' ', _prof_exp_color[exp_lvl], _prof_exp_str[exp_lvl]);
            break;
    }
    doc_newline(doc);
}

static void _prof_skill_doc(doc_ptr doc, int mode)
{
    doc_insert_text(doc, TERM_RED, "Miscellaneous");
    doc_newline(doc);
    _prof_skill_aux(doc, SKILL_MARTIAL_ARTS, mode);
    _prof_skill_aux(doc, SKILL_DUAL_WIELDING, mode);
    _prof_skill_aux(doc, SKILL_RIDING, mode);
    doc_newline(doc);
}

static int _do_cmd_knowledge_weapon_exp_aux(int mode, int *huippu)
{
    doc_ptr doc = doc_alloc(80);
    doc_ptr cols[3] = {0};
    int     i, tulos;

    for (i = 0; i < 3; i++)
        cols[i] = doc_alloc(26);

    _prof_weapon_doc(cols[0], TV_SWORD, mode);
    _prof_weapon_doc(cols[1], TV_POLEARM, mode);
    _prof_weapon_doc(cols[1], TV_BOW, mode);
    _prof_weapon_doc(cols[2], TV_HAFTED, mode);
    _prof_weapon_doc(cols[2], TV_DIGGING, mode);
    _prof_skill_doc(cols[2], mode);

    doc_insert_cols(doc, cols, 3, 1);
    switch (mode)
    {   
        case 1:
        {
            class_t *class_ptr = get_class();
            char buf[64];
            strcpy(buf, class_ptr->name);
            strcat(buf, " Proficiency Caps");
            tulos = weapon_exp_display(doc, buf, huippu); break;
        }
        case 2: tulos = weapon_exp_display(doc, "Current Proficiency as % of Caps", huippu); break;
        case 3: tulos = weapon_exp_display(doc, "Current Proficiency as % of Full Mastery", huippu); break;
        default: tulos = weapon_exp_display(doc, "Current Proficiency", huippu); break;
    }

    doc_free(doc);
    for (i = 0; i < 3; i++)
        doc_free(cols[i]);
    return tulos;
}

static void do_cmd_knowledge_weapon_exp(void)
{
    int mode = 0;
    bool lopeta = FALSE;
    int huippu = 0;

    while (!lopeta)
    {
        if (_do_cmd_knowledge_weapon_exp_aux(mode, &huippu)) mode = ((mode + 1) % 4);
        else lopeta = TRUE;
    } 
}

/*
 * Display spell-exp
 */
static void do_cmd_knowledge_spell_exp(void)
{
    doc_ptr doc = doc_alloc(80);

    doc_insert(doc, "<style:wide>");
    spellbook_character_dump(doc);
    doc_insert(doc, "</style>");
    doc_display(doc, "Spell Proficiency", 0);
    doc_free(doc);
}

/*
 * Pluralize a monster name
 */
static bool _plural_imp(char *name, const char *suffix, const char *replacement)
{
    bool result = FALSE;
    int l1 = strlen(name);
    int l2 = strlen(suffix);

    if (l1 >= l2)
    {
        char *tmp = name + (l1 - l2);
        if (streq(tmp, suffix))
        {
            strcpy(tmp, replacement);
            result = TRUE;
        }
    }
    return result;
}

void plural_aux(char *Name)
{
    int NameLen = strlen(Name);

    if (my_strstr(Name, "Disembodied hand"))
    {
        strcpy(Name, "Disembodied hands that strangled people");
    }
    else if (my_strstr(Name, "Colour out of space"))
    {
        strcpy(Name, "Colours out of space");
    }
    else if (my_strstr(Name, "stairway to hell"))
    {
        strcpy(Name, "stairways to hell");
    }
    else if (my_strstr(Name, "Dweller on the threshold"))
    {
        strcpy(Name, "Dwellers on the threshold");
    }
    else if (my_strstr(Name, " of "))
    {
        cptr aider = my_strstr(Name, " of ");
        char dummy[80];
        int i = 0;
        cptr ctr = Name;

        while (ctr < aider)
        {
            dummy[i] = *ctr;
            ctr++; i++;
        }

        if (dummy[i-1] == 's')
        {
            strcpy(&(dummy[i]), "es");
            i++;
        }
        else
        {
            strcpy(&(dummy[i]), "s");
        }

        strcpy(&(dummy[i+1]), aider);
        strcpy(Name, dummy);
    }
    else if (my_strstr(Name, "coins"))
    {
        char dummy[80];
        strcpy(dummy, "piles of ");
        strcat(dummy, Name);
        strcpy(Name, dummy);
        return;
    }
    else if (my_strstr(Name, "Manes"))
    {
        return;
    }
    else if (_plural_imp(Name, "ey", "eys"))
    {
    }
    else if (_plural_imp(Name, "y", "ies"))
    {
    }
    else if (_plural_imp(Name, "ouse", "ice"))
    {
    }
    else if (_plural_imp(Name, "us", "i"))
    {
    }
    else if (_plural_imp(Name, "kelman", "kelmen"))
    {
    }
    else if (_plural_imp(Name, "wordsman", "wordsmen"))
    {
    }
    else if (_plural_imp(Name, "oodsman", "oodsmen"))
    {
    }
    else if (_plural_imp(Name, "eastman", "eastmen"))
    {
    }
    else if (_plural_imp(Name, "izardman", "izardmen"))
    {
    }
    else if (_plural_imp(Name, "geist", "geister"))
    {
    }
    else if (_plural_imp(Name, "ex", "ices"))
    {
    }
    else if (_plural_imp(Name, "lf", "lves"))
    {
    }
    else if (suffix(Name, "ch") ||
         suffix(Name, "sh") ||
             suffix(Name, "nx") ||
             suffix(Name, "s") ||
             suffix(Name, "o"))
    {
        strcpy(&(Name[NameLen]), "es");
    }
    else
    {
        strcpy(&(Name[NameLen]), "s");
    }
}

/*
 * Display current pets
 */
static void do_cmd_knowledge_pets(void)
{
    int             i;
    FILE            *fff;
    monster_type    *m_ptr;
    monster_race    *r_ptr;
    char            pet_name[80];
    int             t_friends = 0;
    int             show_upkeep = 0;
    char            file_name[1024];


    /* Open a new file */
    fff = my_fopen_temp(file_name, 1024);
    if (!fff) {
        msg_format("Failed to create temporary file %s.", file_name);
        msg_print(NULL);
        return;
    }

    /* Process the monsters (backwards) */
    for (i = m_max - 1; i >= 1; i--)
    {
        /* Access the monster */
        m_ptr = &m_list[i];
        r_ptr = &r_info[m_ptr->r_idx];

        /* Ignore "dead" monsters */
        if (!m_ptr->r_idx) continue;

        /* Calculate "upkeep" for pets */
        if (is_pet(m_ptr))
        {
            t_friends++;
            monster_desc(pet_name, m_ptr, MD_ASSUME_VISIBLE | MD_INDEF_VISIBLE);
            fprintf(fff, "%s (", pet_name);
            if (r_ptr->r_tkills)
                fprintf(fff, "L%d, ", r_ptr->level);
            fprintf(fff, "%s)\n", mon_health_desc(m_ptr));
        }
    }

    show_upkeep = calculate_upkeep();

    fprintf(fff, "----------------------------------------------\n");
    fprintf(fff, "   Total: %d pet%s.\n",
        t_friends, (t_friends == 1 ? "" : "s"));
    fprintf(fff, "   Upkeep: %d%% mana.\n", show_upkeep);



    /* Close the file */
    my_fclose(fff);

    /* Display the file contents */
    show_file(TRUE, file_name, "Current Pets", 0, 0);


    /* Remove the file */
    fd_kill(file_name);
}


/*
 * Total kill count
 *
 * Note that the player ghosts are ignored. XXX XXX XXX
 */
static void do_cmd_knowledge_kill_count(void)
{
    int i, k, n = 0;
    u16b why = 2;
    s16b *who;

    FILE *fff;

    char file_name[1024];

    s32b Total = 0;


    /* Open a new file */
    fff = my_fopen_temp(file_name, 1024);

    if (!fff) {
        msg_format("Failed to create temporary file %s.", file_name);
        msg_print(NULL);
        return;
    }

    /* Allocate the "who" array */
    C_MAKE(who, max_r_idx, s16b);

    {
        /* Monsters slain */
        int kk;

        for (kk = 1; kk < max_r_idx; kk++)
        {
            monster_race *r_ptr = &r_info[kk];

            if (r_ptr->flags1 & (RF1_UNIQUE))
            {
                bool dead = (r_ptr->max_num == 0);

                if (dead)
                {
                    Total++;
                }
            }
            else
            {
                s16b This = r_ptr->r_pkills;

                if (This > 0)
                {
                    Total += This;
                }
            }
        }

        if (Total < 1)
            fprintf(fff,"You have defeated no enemies yet.\n\n");
        else
            fprintf(fff,"You have defeated %d %s.\n\n", Total, (Total == 1) ? "enemy" : "enemies");
    }

    Total = 0;

    /* Scan the monsters */
    for (i = 1; i < max_r_idx; i++)
    {
        monster_race *r_ptr = &r_info[i];

        /* Use that monster */
        if (r_ptr->name) who[n++] = i;
    }

    /* Select the sort method */
    ang_sort_comp = ang_sort_comp_hook;
    ang_sort_swap = ang_sort_swap_hook;

    /* Sort the array by dungeon depth of monsters */
    ang_sort(who, &why, n);

    /* Scan the monster races */
    for (k = 0; k < n; k++)
    {
        monster_race *r_ptr = &r_info[who[k]];

        if (r_ptr->flags1 & (RF1_UNIQUE))
        {
            bool dead = (r_ptr->max_num == 0);

            if (dead)
            {
                /* Print a message */
                fprintf(fff, "     %s\n",
                    (r_name + r_ptr->name));
                Total++;
            }
        }
        else
        {
            s16b This = r_ptr->r_pkills;

            if (This > 0)
            {
                if (This < 2)
                {
                    if (my_strstr(r_name + r_ptr->name, "coins"))
                    {
                        fprintf(fff, "     1 pile of %s\n", (r_name + r_ptr->name));
                    }
                    else
                    {
                        fprintf(fff, "     1 %s\n", (r_name + r_ptr->name));
                    }
                }
                else
                {
                    char ToPlural[80];
                    strcpy(ToPlural, (r_name + r_ptr->name));
                    plural_aux(ToPlural);
                    fprintf(fff, "     %d %s\n", This, ToPlural);
                }


                Total += This;
            }
        }
    }

    fprintf(fff,"----------------------------------------------\n");
    fprintf(fff,"   Total: %d creature%s killed.\n",
        Total, (Total == 1 ? "" : "s"));


    /* Free the "who" array */
    C_KILL(who, max_r_idx, s16b);

    /* Close the file */
    my_fclose(fff);

    /* Display the file contents */
    show_file(TRUE, file_name, "Kill Count", 0, 0);


    /* Remove the file */
    fd_kill(file_name);
}


/*
 * Display the object groups.
 */
static void display_group_list(int col, int row, int wid, int per_page,
    int grp_idx[], cptr group_text[], int grp_cur, int grp_top)
{
    int i;

    /* Display lines until done */
    for (i = 0; i < per_page && (grp_idx[i] >= 0); i++)
    {
        /* Get the group index */
        int grp = grp_idx[grp_top + i];

        /* Choose a color */
        byte attr = (grp_top + i == grp_cur) ? TERM_L_BLUE : TERM_WHITE;

        /* Erase the entire line */
        Term_erase(col, row + i, wid);

        /* Display the group label */
        c_put_str(attr, group_text[grp], row + i, col);
    }
}


/*
 * Move the cursor in a browser window
 */
static void browser_cursor(char ch, int *column, int *grp_cur, int grp_cnt,
                           int *list_cur, int list_cnt)
{
    int d;
    int col = *column;
    int grp = *grp_cur;
    int list = *list_cur;

    /* Extract direction */
    if (ch == ' ')
    {
        /* Hack -- scroll up full screen */
        d = 3;
    }
    else if (ch == '-')
    {
        /* Hack -- scroll down full screen */
        d = 9;
    }
    else
    {
        d = get_keymap_dir(ch, FALSE);
    }

    if (!d) return;

    /* Diagonals - hack */
    if ((ddx[d] > 0) && ddy[d])
    {
        int browser_rows;
        int wid, hgt;

        /* Get size */
        Term_get_size(&wid, &hgt);

        browser_rows = hgt - 8;

        /* Browse group list */
        if (!col)
        {
            int old_grp = grp;

            /* Move up or down */
            grp += ddy[d] * (browser_rows - 1);

            /* Verify */
            if (grp >= grp_cnt)    grp = grp_cnt - 1;
            if (grp < 0) grp = 0;
            if (grp != old_grp)    list = 0;
        }

        /* Browse sub-list list */
        else
        {
            /* Move up or down */
            list += ddy[d] * browser_rows;

            /* Verify */
            if (list >= list_cnt) list = list_cnt - 1;
            if (list < 0) list = 0;
        }

        (*grp_cur) = grp;
        (*list_cur) = list;

        return;
    }

    if (ddx[d])
    {
        col += ddx[d];
        if (col < 0) col = 0;
        if (col > 1) col = 1;

        (*column) = col;

        return;
    }

    /* Browse group list */
    if (!col)
    {
        int old_grp = grp;

        /* Move up or down */
        grp += ddy[d];

        /* Verify */
        if (grp >= grp_cnt)    grp = grp_cnt - 1;
        if (grp < 0) grp = 0;
        if (grp != old_grp)    list = 0;
    }

    /* Browse sub-list list */
    else
    {
        /* Move up or down */
        list += ddy[d];

        /* Verify */
        if (list >= list_cnt) list = list_cnt - 1;
        if (list < 0) list = 0;
    }

    (*grp_cur) = grp;
    (*list_cur) = list;
}


/*
 * Display visuals.
 */
static void display_visual_list(int col, int row, int height, int width, byte attr_top, byte char_left)
{
    int i, j;

    /* Clear the display lines */
    for (i = 0; i < height; i++)
    {
        Term_erase(col, row + i, width);
    }

    /* Bigtile mode uses double width */
    if (use_bigtile) width /= 2;

    /* Display lines until done */
    for (i = 0; i < height; i++)
    {
        /* Display columns until done */
        for (j = 0; j < width; j++)
        {
            byte a;
            char c;
            int x = col + j;
            int y = row + i;
            int ia, ic;

            /* Bigtile mode uses double width */
            if (use_bigtile) x += j;

            ia = attr_top + i;
            ic = char_left + j;

            /* Ignore illegal characters */
            if (ia > 0x7f || ic > 0xff || ic < ' ' ||
                (!use_graphics && ic > 0x7f))
                continue;

            a = (byte)ia;
            c = (char)ic;

            /* Force correct code for both ASCII character and tile */
            if (c & 0x80) a |= 0x80;

            /* Display symbol */
            Term_queue_bigchar(x, y, a, c, 0, 0);
        }
    }
}


/*
 * Place the cursor at the collect position for visual mode
 */
static void place_visual_list_cursor(int col, int row, byte a, byte c, byte attr_top, byte char_left)
{
    int i = (a & 0x7f) - attr_top;
    int j = c - char_left;

    int x = col + j;
    int y = row + i;

    /* Bigtile mode uses double width */
    if (use_bigtile) x += j;

    /* Place the cursor */
    Term_gotoxy(x, y);
}


/*
 *  Clipboard variables for copy&paste in visual mode
 */
static byte attr_idx = 0;
static byte char_idx = 0;

/* Hack -- for feature lighting */
static byte attr_idx_feat[F_LIT_MAX];
static byte char_idx_feat[F_LIT_MAX];

/*
 *  Do visual mode command -- Change symbols
 */
static bool visual_mode_command(char ch, bool *visual_list_ptr,
                int height, int width,
                byte *attr_top_ptr, byte *char_left_ptr,
                byte *cur_attr_ptr, byte *cur_char_ptr, bool *need_redraw)
{
    static byte attr_old = 0, char_old = 0;

    switch (ch)
    {
    case ESCAPE:
        if (*visual_list_ptr)
        {
            /* Cancel change */
            *cur_attr_ptr = attr_old;
            *cur_char_ptr = char_old;
            *visual_list_ptr = FALSE;

            return TRUE;
        }
        break;

    case '\n':
    case '\r':
        if (*visual_list_ptr)
        {
            /* Accept change */
            *visual_list_ptr = FALSE;
            *need_redraw = TRUE;

            return TRUE;
        }
        break;

    case 'V':
    case 'v':
        if (!*visual_list_ptr)
        {
            *visual_list_ptr = TRUE;

            *attr_top_ptr = MAX(0, (*cur_attr_ptr & 0x7f) - 5);
            *char_left_ptr = MAX(0, *cur_char_ptr - 10);

            attr_old = *cur_attr_ptr;
            char_old = *cur_char_ptr;

            return TRUE;
        }
        break;

    case 'C':
    case 'c':
        {
            int i;

            /* Set the visual */
            attr_idx = *cur_attr_ptr;
            char_idx = *cur_char_ptr;

            /* Hack -- for feature lighting */
            for (i = 0; i < F_LIT_MAX; i++)
            {
                attr_idx_feat[i] = 0;
                char_idx_feat[i] = 0;
            }
        }
        return TRUE;

    case 'P':
    case 'p':
        if (attr_idx || (!(char_idx & 0x80) && char_idx)) /* Allow TERM_DARK text */
        {
            /* Set the char */
            *cur_attr_ptr = attr_idx;
            *attr_top_ptr = MAX(0, (*cur_attr_ptr & 0x7f) - 5);
            if (!*visual_list_ptr) *need_redraw = TRUE;
        }

        if (char_idx)
        {
            /* Set the char */
            *cur_char_ptr = char_idx;
            *char_left_ptr = MAX(0, *cur_char_ptr - 10);
            if (!*visual_list_ptr) *need_redraw = TRUE;
        }

        return TRUE;

    default:
        if (*visual_list_ptr)
        {
            int eff_width;
            int d = get_keymap_dir(ch, FALSE);
            byte a = (*cur_attr_ptr & 0x7f);
            byte c = *cur_char_ptr;

            if (use_bigtile) eff_width = width / 2;
            else eff_width = width;

            /* Restrict direction */
            if ((a == 0) && (ddy[d] < 0)) d = 0;
            if ((c == 0) && (ddx[d] < 0)) d = 0;
            if ((a == 0x7f) && (ddy[d] > 0)) d = 0;
            if ((c == 0xff) && (ddx[d] > 0)) d = 0;

            a += ddy[d];
            c += ddx[d];

            /* Force correct code for both ASCII character and tile */
            if (c & 0x80) a |= 0x80;

            /* Set the visual */
            *cur_attr_ptr = a;
            *cur_char_ptr = c;


            /* Move the frame */
            if ((ddx[d] < 0) && *char_left_ptr > MAX(0, (int)c - 10)) (*char_left_ptr)--;
            if ((ddx[d] > 0) && *char_left_ptr + eff_width < MIN(0xff, (int)c + 10)) (*char_left_ptr)++;
            if ((ddy[d] < 0) && *attr_top_ptr > MAX(0, (int)(a & 0x7f) - 4)) (*attr_top_ptr)--;
            if ((ddy[d] > 0) && *attr_top_ptr + height < MIN(0x7f, (a & 0x7f) + 4)) (*attr_top_ptr)++;
            return TRUE;
        }
        break;
    }

    /* Visual mode command is not used */
    return FALSE;
}

enum monster_mode_e
{
    MONSTER_MODE_STATS,
    MONSTER_MODE_SKILLS,
    MONSTER_MODE_EXTRA,
    MONSTER_MODE_MAX
};
static int monster_mode = MONSTER_MODE_STATS;

static void _prt_equippy(int col, int row, int tval, int sval)
{
    int k_idx = lookup_kind(tval, sval);
    object_kind *k_ptr = &k_info[k_idx];
    Term_putch(col, row, k_ptr->x_attr, k_ptr->x_char);
}

/*
 * Display the monsters in a group.
 */
static void display_monster_list(int col, int row, int per_page, s16b mon_idx[],
    int mon_cur, int mon_top, bool visual_only)
{
    int i;

    /* Display lines until done */
    for (i = 0; i < per_page && (mon_idx[mon_top + i] >= 0); i++)
    {
        byte attr;

        /* Get the race index */
        int r_idx = mon_idx[mon_top + i] ;

        /* Access the race */
        monster_race *r_ptr = &r_info[r_idx];

        /* Choose a color */
        attr = ((i + mon_top == mon_cur) ? TERM_L_BLUE : TERM_WHITE);
        if (attr == TERM_WHITE && (r_ptr->flagsx & RFX_SUPPRESS))
            attr = TERM_L_DARK;

        /* Display the name */
        c_prt(attr, (r_name + r_ptr->name), row + i, col);

        /* Hack -- visual_list mode */
        if (per_page == 1)
        {
            c_prt(attr, format("%02x/%02x", r_ptr->x_attr, r_ptr->x_char), row + i, (p_ptr->wizard || visual_only) ? 56 : 61);
        }
        if (p_ptr->wizard || visual_only)
        {
            c_prt(attr, format("%d", r_idx), row + i, 62);
        }

        /* Erase chars before overwritten by the race letter */
        Term_erase(69, row + i, 255);

        /* Display symbol */
        Term_queue_bigchar(use_bigtile ? 69 : 70, row + i, r_ptr->x_attr, r_ptr->x_char, 0, 0);

        if (!visual_only)
        {
            /* Display kills */
            if (!(r_ptr->flags1 & RF1_UNIQUE)) put_str(format("%5d", r_ptr->r_pkills), row + i, 73);
            else c_put_str((r_ptr->max_num == 0 ? TERM_L_DARK : TERM_WHITE), (r_ptr->max_num == 0 ? " dead" : "alive"), row + i, 73);

            /* Only Possessors get the extra body info display */
            if (p_ptr->wizard || p_ptr->prace == RACE_MON_POSSESSOR || p_ptr->prace == RACE_MON_MIMIC)
            {
                /* And then, they must learn about the body first. (Or be a cheating wizard :) */
                if ((p_ptr->wizard || (r_ptr->r_xtra1 & MR1_POSSESSOR)) && r_ptr->body.life)
                {
                    char buf[255];
                    equip_template_ptr body = &b_info[r_ptr->body.body_idx];
                    if (monster_mode == MONSTER_MODE_STATS)
                    {
                        int j;
                        for (j = 0; j < 6; j++)
                        {
                            sprintf(buf, "%+3d", r_ptr->body.stats[j]);
                            c_put_str(j == r_ptr->body.spell_stat ? TERM_L_GREEN : TERM_WHITE,
                                      buf, row + i, 80 + j * 5);
                        }
                        sprintf(buf, "%+3d%%", r_ptr->body.life);
                        c_put_str(TERM_WHITE, buf, row + i, 110);

                        for (j = 1; j <= body->max; j++)
                        {
                            int c = 115 + j;
                            int r = row + i;
                            switch (body->slots[j].type)
                            {
                            case EQUIP_SLOT_GLOVES:
                                _prt_equippy(c, r, TV_GLOVES, SV_SET_OF_GAUNTLETS);
                                break;
                            case EQUIP_SLOT_WEAPON_SHIELD:
                                if (body->slots[j].hand % 2)
                                    _prt_equippy(c, r, TV_SHIELD, SV_LARGE_METAL_SHIELD);
                                else
                                    _prt_equippy(c, r, TV_SWORD, SV_LONG_SWORD);
                                break;
                            case EQUIP_SLOT_WEAPON:
                                _prt_equippy(c, r, TV_SWORD, SV_LONG_SWORD);
                                break;
                            case EQUIP_SLOT_RING:
                                _prt_equippy(c, r, TV_RING, 0);
                                break;
                            case EQUIP_SLOT_BOW:
                                _prt_equippy(c, r, TV_BOW, SV_LONG_BOW);
                                break;
                            case EQUIP_SLOT_AMULET:
                                _prt_equippy(c, r, TV_AMULET, 0);
                                break;
                            case EQUIP_SLOT_LITE:
                                _prt_equippy(c, r, TV_LITE, SV_LITE_FEANOR);
                                break;
                            case EQUIP_SLOT_BODY_ARMOR:
                                _prt_equippy(c, r, TV_HARD_ARMOR, SV_CHAIN_MAIL);
                                break;
                            case EQUIP_SLOT_CLOAK:
                                _prt_equippy(c, r, TV_CLOAK, SV_CLOAK);
                                break;
                            case EQUIP_SLOT_BOOTS:
                                _prt_equippy(c, r, TV_BOOTS, SV_PAIR_OF_HARD_LEATHER_BOOTS);
                                break;
                            case EQUIP_SLOT_HELMET:
                                _prt_equippy(c, r, TV_HELM, SV_IRON_HELM);
                                break;
                            case EQUIP_SLOT_ANY:
                                Term_putch(c, r, TERM_WHITE, '*');
                                break;
                            case EQUIP_SLOT_CAPTURE_BALL:
                                _prt_equippy(c, r, TV_CAPTURE, 0);
                                break;
                            }
                        }
                    }
                    else if (monster_mode == MONSTER_MODE_SKILLS)
                    {
                        sprintf(buf, "%2d+%-2d  %2d+%-2d  %2d+%-2d  %4d  %4d  %4d  %2d+%-2d  %2d+%-2d\n",
                            r_ptr->body.skills.dis, r_ptr->body.extra_skills.dis,
                            r_ptr->body.skills.dev, r_ptr->body.extra_skills.dev,
                            r_ptr->body.skills.sav, r_ptr->body.extra_skills.sav,
                            r_ptr->body.skills.stl,
                            r_ptr->body.skills.srh,
                            r_ptr->body.skills.fos,
                            r_ptr->body.skills.thn, r_ptr->body.extra_skills.thn,
                            r_ptr->body.skills.thb, r_ptr->body.extra_skills.thb
                        );
                        c_put_str(TERM_WHITE, buf, row + i, 80);
                    }
                    else if (monster_mode == MONSTER_MODE_EXTRA)
                    {
                        int speed = possessor_r_speed(r_idx);
                        int ac = possessor_r_ac(r_idx);

                        sprintf(buf, "%3d  %3d  %+5d  %+4d  %s",
                            r_ptr->level, possessor_max_plr_lvl(r_idx), speed, ac,
                            get_class_aux(r_ptr->body.class_idx, 0)->name
                        );
                        c_put_str(TERM_WHITE, buf, row + i, 80);
                    }
                }
            }
        }
    }

    /* Clear remaining lines */
    for (; i < per_page; i++)
    {
        Term_erase(col, row + i, 255);
    }
}


/*
 * Display known monsters.
 */
static void do_cmd_knowledge_monsters(bool *need_redraw, bool visual_only, int direct_r_idx)
{
    int i, len, max;
    int grp_cur, grp_top, old_grp_cur;
    int mon_cur, mon_top;
    int grp_cnt, grp_idx[100];
    int mon_cnt;
    s16b *mon_idx;

    int column = 0;
    bool flag;
    bool redraw;

    bool visual_list = FALSE;
    byte attr_top = 0, char_left = 0;

    int browser_rows;
    int wid, hgt;

    byte mode;

    /* Get size */
    Term_get_size(&wid, &hgt);

    browser_rows = hgt - 8;

    /* Allocate the "mon_idx" array */
    C_MAKE(mon_idx, max_r_idx, s16b);

    max = 0;
    grp_cnt = 0;

    if (direct_r_idx < 0)
    {
        mode = visual_only ? 0x03 : 0x01;

        /* Check every group */
        for (i = 0; monster_group_text[i] != NULL; i++)
        {
            if (monster_group_char[i] == ((char *) -1L) && p_ptr->prace != RACE_MON_POSSESSOR)
                continue;

            /* Measure the label */
            len = strlen(monster_group_text[i]);

            /* Save the maximum length */
            if (len > max) max = len;

            /* See if any monsters are known */
            if ((monster_group_char[i] == ((char *) -2L)) || collect_monsters(i, mon_idx, mode))
            {
                /* Build a list of groups with known monsters */
                grp_idx[grp_cnt++] = i;
            }
        }

        mon_cnt = 0;
    }
    else
    {
        mon_idx[0] = direct_r_idx;
        mon_cnt = 1;

        /* Terminate the list */
        mon_idx[1] = -1;

        (void)visual_mode_command('v', &visual_list, browser_rows - 1, wid - (max + 3),
            &attr_top, &char_left, &r_info[direct_r_idx].x_attr, &r_info[direct_r_idx].x_char, need_redraw);
    }

    /* Terminate the list */
    grp_idx[grp_cnt] = -1;

    old_grp_cur = -1;
    grp_cur = grp_top = 0;
    mon_cur = mon_top = 0;

    flag = FALSE;
    redraw = TRUE;

    mode = visual_only ? 0x02 : 0x00;

    while (!flag)
    {
        char ch;
        monster_race *r_ptr;

        if (redraw)
        {
            clear_from(0);

            prt(format("%s - Monsters", !visual_only ? "Knowledge" : "Visuals"), 2, 0);
            if (direct_r_idx < 0) prt("Group", 4, 0);
            prt("Name", 4, max + 3);
            if (p_ptr->wizard || visual_only) prt("Idx", 4, 62);
            prt("Sym", 4, 68);
            if (!visual_only) prt("Kills", 4, 73);

            if (p_ptr->wizard || p_ptr->prace == RACE_MON_POSSESSOR || p_ptr->prace == RACE_MON_MIMIC)
            {
                char buf[255];
                if (monster_mode == MONSTER_MODE_STATS)
                {
                    sprintf(buf, "STR  INT  WIS  DEX  CON  CHR  Life  Body");
                    c_put_str(TERM_WHITE, buf, 4, 80);
                    for (i = 78; i < 130; i++)
                        Term_putch(i, 5, TERM_WHITE, '=');
                }
                else if (monster_mode == MONSTER_MODE_SKILLS)
                {
                    sprintf(buf, "Dsrm   Dvce   Save   Stlh  Srch  Prcp  Melee  Bows");
                    c_put_str(TERM_WHITE, buf, 4, 80);
                    for (i = 78; i < 130; i++)
                        Term_putch(i, 5, TERM_WHITE, '=');
                }
                else if (monster_mode == MONSTER_MODE_EXTRA)
                {
                    sprintf(buf, "Lvl  Max  Speed    AC  Pseudo-Class");
                    c_put_str(TERM_WHITE, buf, 4, 80);
                    for (i = 78; i < 130; i++)
                        Term_putch(i, 5, TERM_WHITE, '=');
                }
            }

            for (i = 0; i < 78; i++)
            {
                Term_putch(i, 5, TERM_WHITE, '=');
            }

            if (direct_r_idx < 0)
            {
                for (i = 0; i < browser_rows; i++)
                {
                    Term_putch(max + 1, 6 + i, TERM_WHITE, '|');
                }
            }

            redraw = FALSE;
        }

        if (direct_r_idx < 0)
        {
            /* Scroll group list */
            if (grp_cur < grp_top) grp_top = grp_cur;
            if (grp_cur >= grp_top + browser_rows) grp_top = grp_cur - browser_rows + 1;

            /* Display a list of monster groups */
            display_group_list(0, 6, max, browser_rows, grp_idx, monster_group_text, grp_cur, grp_top);

            if (old_grp_cur != grp_cur)
            {
                old_grp_cur = grp_cur;

                /* Get a list of monsters in the current group */
                mon_cnt = collect_monsters(grp_idx[grp_cur], mon_idx, mode);
            }

            /* Scroll monster list */
            while (mon_cur < mon_top)
                mon_top = MAX(0, mon_top - browser_rows/2);
            while (mon_cur >= mon_top + browser_rows)
                mon_top = MIN(mon_cnt - browser_rows, mon_top + browser_rows/2);
        }

        if (!visual_list)
        {
            /* Display a list of monsters in the current group */
            display_monster_list(max + 3, 6, browser_rows, mon_idx, mon_cur, mon_top, visual_only);
        }
        else
        {
            mon_top = mon_cur;

            /* Display a monster name */
            display_monster_list(max + 3, 6, 1, mon_idx, mon_cur, mon_top, visual_only);

            /* Display visual list below first monster */
            display_visual_list(max + 3, 7, browser_rows-1, wid - (max + 3), attr_top, char_left);
        }

        /* Prompt */
        if (p_ptr->wizard || p_ptr->prace == RACE_MON_POSSESSOR || p_ptr->prace == RACE_MON_MIMIC)
        {
            prt(format("<dir>%s%s%s%s, ESC",
                (!visual_list && !visual_only) ? ", '?' to recall" : "",
                visual_list ? ", ENTER to accept" : ", 'v' for visuals",
                (attr_idx || char_idx) ? ", 'c', 'p' to paste" : ", 'c' to copy",
                ", '=' for more info"),
                hgt - 1, 0);
        }
        else
        {
            prt(format("<dir>%s%s%s, ESC",
                (!visual_list && !visual_only) ? ", '?' to recall" : "",
                visual_list ? ", ENTER to accept" : ", 'v' for visuals",
                (attr_idx || char_idx) ? ", 'c', 'p' to paste" : ", 'c' to copy"),
                hgt - 1, 0);
        }

        /* Get the current monster */
        r_ptr = &r_info[mon_idx[mon_cur]];

        if (!visual_only)
        {
            /* Mega Hack -- track this monster race */
            if (mon_cnt) monster_race_track(mon_idx[mon_cur]);

            /* Hack -- handle stuff */
            handle_stuff();
        }

        if (visual_list)
        {
            place_visual_list_cursor(max + 3, 7, r_ptr->x_attr, r_ptr->x_char, attr_top, char_left);
        }
        else if (!column)
        {
            Term_gotoxy(0, 6 + (grp_cur - grp_top));
        }
        else
        {
            Term_gotoxy(max + 3, 6 + (mon_cur - mon_top));
        }

        ch = inkey();

        /* Do visual mode command if needed */
        if (visual_mode_command(ch, &visual_list, browser_rows-1, wid - (max + 3), &attr_top, &char_left, &r_ptr->x_attr, &r_ptr->x_char, need_redraw))
        {
            if (direct_r_idx >= 0)
            {
                switch (ch)
                {
                case '\n':
                case '\r':
                case ESCAPE:
                    flag = TRUE;
                    break;
                }
            }
            continue;
        }

        switch (ch)
        {
            case ESCAPE:
            {
                flag = TRUE;
                break;
            }

            case 'R':
            case 'r':
            case '?':
            {
                /* Recall on screen */
                if (!visual_list && !visual_only && (mon_idx[mon_cur] > 0))
                {
                    int r_idx = mon_idx[mon_cur];
                    mon_display(&r_info[r_idx]);
                    redraw = TRUE;
                }
                break;
            }

            case 'm':
            case 'n':
            case 'h':
            case '=':
                monster_mode++;
                if (monster_mode == MONSTER_MODE_MAX)
                    monster_mode = MONSTER_MODE_STATS;
                redraw = TRUE;
                break;

            default:
            {
                /* Move the cursor */
                browser_cursor(ch, &column, &grp_cur, grp_cnt, &mon_cur, mon_cnt);

                break;
            }
        }
    }

    /* Free the "mon_idx" array */
    C_KILL(mon_idx, max_r_idx, s16b);
}


/*
 * Display the objects in a group.
 */
static void display_object_list(int col, int row, int per_page, int object_idx[],
    int object_cur, int object_top, int object_count, bool visual_only)
{
    int i;

    /* Display lines until done */
    for (i = 0; i < per_page && object_top + i < object_count && object_idx[object_top + i] >= 0; i++)
    {
        char o_name[80];
        char buf[255];
        byte a, c;
        object_kind *flavor_k_ptr;

        /* Get the object index */
        int k_idx = object_idx[object_top + i];

        /* Access the object */
        object_kind *k_ptr = &k_info[k_idx];

        /* Choose a color */
        byte attr = ((k_ptr->aware || visual_only) ? TERM_WHITE : TERM_SLATE);
        byte cursor = ((k_ptr->aware || visual_only) ? TERM_L_BLUE : TERM_BLUE);


        if (!visual_only && k_ptr->flavor)
        {
            /* Appearance of this object is shuffled */
            flavor_k_ptr = &k_info[k_ptr->flavor];
        }
        else
        {
            /* Appearance of this object is very normal */
            flavor_k_ptr = k_ptr;
        }



        attr = ((i + object_top == object_cur) ? cursor : attr);

        if (!k_ptr->flavor || (!visual_only && k_ptr->aware))
        {
            /* Tidy name */
            strip_name(o_name, k_idx);
        }
        else
        {
            /* Flavor name */
            strcpy(o_name, k_name + flavor_k_ptr->flavor_name);
        }

        /* Display the name */
        sprintf(buf, "%-35.35s %5d %6d %4d %4d", o_name, k_ptr->counts.found, k_ptr->counts.bought, k_ptr->counts.used, k_ptr->counts.destroyed);
        c_prt(attr, buf, row + i, col);

        /* Hack -- visual_list mode */
        if (per_page == 1)
        {
            c_prt(attr, format("%02x/%02x", flavor_k_ptr->x_attr, flavor_k_ptr->x_char), row + i, (p_ptr->wizard || visual_only) ? 64 : 68);
        }
        if (visual_only)
        {
            c_prt(attr, format("%d", k_idx), row + i, 70);
        }

        a = flavor_k_ptr->x_attr;
        c = flavor_k_ptr->x_char;

        /* Display symbol */
        Term_queue_bigchar(use_bigtile ? 76 : 77, row + i, a, c, 0, 0);
    }

    /* Total Line? */
    if (!visual_only && i < per_page && object_idx[object_top + i] < 0)
    {
        char     buf[255];
        counts_t totals = {0};
        int      j;

        for (j = 0; object_idx[j] >= 0; j++)
        {
            object_kind   *k_ptr = &k_info[object_idx[j]];

            totals.found += k_ptr->counts.found;
            totals.bought += k_ptr->counts.bought;
            totals.used += k_ptr->counts.used;
            totals.destroyed += k_ptr->counts.destroyed;
        }

        sprintf(buf, "%-35.35s %5d %6d %4d %4d",
            "Totals",
            totals.found, totals.bought, totals.used, totals.destroyed
        );
        c_prt(TERM_YELLOW, buf, row + i, col);
        i++;
    }

    /* Clear remaining lines */
    for (; i < per_page; i++)
    {
        Term_erase(col, row + i, 255);
    }
}

/*
 * Describe fake object
 */
static void desc_obj_fake(int k_idx)
{
    object_type *o_ptr;
    object_type object_type_body;

    /* Get local object */
    o_ptr = &object_type_body;

    /* Wipe the object */
    object_wipe(o_ptr);

    /* Create the artifact */
    object_prep(o_ptr, k_idx);

    /* It's fully know */
    o_ptr->ident |= IDENT_KNOWN;

    /* Track the object */
    /* object_actual_track(o_ptr); */

    /* Hack - mark as fake */
    /* term_obj_real = FALSE; */

    /* Hack -- Handle stuff */
    handle_stuff();

    obj_display(o_ptr);
}

static void desc_ego_fake(int e_idx)
{
    ego_type *e_ptr = &e_info[e_idx];
    ego_display(e_ptr);
}


typedef struct {
    u32b id;
    cptr name;
} _ego_type_t;

static _ego_type_t _ego_types[] = {
    { EGO_TYPE_WEAPON, "Weapons" },
    { EGO_TYPE_DIGGER, "Diggers" },

    { EGO_TYPE_SHIELD, "Shields" },
    { EGO_TYPE_BODY_ARMOR, "Body Armor" },
    { EGO_TYPE_ROBE, "Robes" },
    { EGO_TYPE_DRAGON_ARMOR, "Dragon Armor" },
    { EGO_TYPE_CLOAK, "Cloaks" },
    { EGO_TYPE_HELMET, "Helmets" },
    { EGO_TYPE_CROWN, "Crowns" },
    { EGO_TYPE_GLOVES, "Gloves" },
    { EGO_TYPE_BOOTS, "Boots" },

    { EGO_TYPE_BOW, "Bows" },
    { EGO_TYPE_AMMO, "Ammo" },
    { EGO_TYPE_HARP, "Harps" },

    { EGO_TYPE_RING, "Rings" },
    { EGO_TYPE_AMULET, "Amulets" },
    { EGO_TYPE_LITE, "Lights" },
    { EGO_TYPE_DEVICE, "Devices" },

    { EGO_TYPE_NONE, NULL },
};

static bool _compare_e_level(vptr u, vptr v, int a, int b)
{
    int *indices = (int*)u;
    int left = indices[a];
    int right = indices[b];
    return e_info[left].level <= e_info[right].level;
}

static int _collect_egos(int grp_cur, int ego_idx[])
{
    int i, cnt = 0;
    int type = _ego_types[grp_cur].id;

    for (i = 0; i < max_e_idx; i++)
    {
        ego_type *e_ptr = &e_info[i];

        if (!e_ptr->name) continue;
        /*if (!e_ptr->aware) continue;*/
        if (!ego_has_lore(e_ptr) && !e_ptr->counts.found && !e_ptr->counts.bought) continue;
        if (!(e_ptr->type & type)) continue;

        ego_idx[cnt++] = i;
    }

    /* Sort Results */
    ang_sort_comp = _compare_e_level;
    ang_sort_swap = _swap_int;
    ang_sort(ego_idx, NULL, cnt);

    /* Terminate the list */
    ego_idx[cnt] = -1;

    return cnt;
}

static void do_cmd_knowledge_egos(void)
{
    int i, len, max;
    int grp_cur, grp_top, old_grp_cur;
    int ego_cur, ego_top;
    int grp_cnt, grp_idx[100];
    int ego_cnt;
    int *ego_idx;

    int column = 0;
    bool flag;
    bool redraw;

    int browser_rows;
    int wid, hgt;

    /* Get size */
    Term_get_size(&wid, &hgt);

    browser_rows = hgt - 8;

    C_MAKE(ego_idx, max_e_idx, int);

    max = 0;
    grp_cnt = 0;
    for (i = 0; _ego_types[i].id != EGO_TYPE_NONE; i++)
    {
        len = strlen(_ego_types[i].name);
        if (len > max)
            max = len;

        if (_collect_egos(i, ego_idx))
            grp_idx[grp_cnt++] = i;
    }
    grp_idx[grp_cnt] = -1;

    if (!grp_cnt)
    {
        prt("You haven't found any egos just yet. Press any key to continue.", 0, 0);
        inkey();
        prt("", 0, 0);
        C_KILL(ego_idx, max_e_idx, int);
        return;
    }

    ego_cnt = 0;

    old_grp_cur = -1;
    grp_cur = grp_top = 0;
    ego_cur = ego_top = 0;

    flag = FALSE;
    redraw = TRUE;

    while (!flag)
    {
        char ch;
        if (redraw)
        {
            clear_from(0);

            prt(format("%s - Egos", "Knowledge"), 2, 0);
            prt("Group", 4, 0);
            prt("Name", 4, max + 3);
            prt("Found Bought Dest", 4, max + 3 + 36);

            for (i = 0; i < 72; i++)
            {
                Term_putch(i, 5, TERM_WHITE, '=');
            }

            for (i = 0; i < browser_rows; i++)
            {
                Term_putch(max + 1, 6 + i, TERM_WHITE, '|');
            }

            redraw = FALSE;
        }

        /* Scroll group list */
        if (grp_cur < grp_top) grp_top = grp_cur;
        if (grp_cur >= grp_top + browser_rows) grp_top = grp_cur - browser_rows + 1;

        /* Display a list of object groups */
        for (i = 0; i < browser_rows && grp_idx[i] >= 0; i++)
        {
            int  grp = grp_idx[grp_top + i];
            byte attr = (grp_top + i == grp_cur) ? TERM_L_BLUE : TERM_WHITE;

            Term_erase(0, 6 + i, max);
            c_put_str(attr, _ego_types[grp].name, 6 + i, 0);
        }

        if (old_grp_cur != grp_cur)
        {
            old_grp_cur = grp_cur;

            /* Get a list of objects in the current group */
            ego_cnt = _collect_egos(grp_idx[grp_cur], ego_idx) + 1;
        }

        /* Scroll object list */
        while (ego_cur < ego_top)
            ego_top = MAX(0, ego_top - browser_rows/2);
        while (ego_cur >= ego_top + browser_rows)
            ego_top = MIN(ego_cnt - browser_rows, ego_top + browser_rows/2);

        /* Display a list of objects in the current group */
        /* Display lines until done */
        for (i = 0; i < browser_rows && ego_top + i < ego_cnt && ego_idx[ego_top + i] >= 0; i++)
        {
            char           buf[255];
            char           name[255];
            int            idx = ego_idx[ego_top + i];
            ego_type      *e_ptr = &e_info[idx];
            byte           attr = TERM_WHITE;

            if (i + ego_top == ego_cur)
                attr = TERM_L_BLUE;

            strip_name_aux(name, e_name + e_ptr->name);
            if (e_ptr->type & (~_ego_types[grp_idx[grp_cur]].id))
                strcat(name, " [Shared]");

            sprintf(buf, "%-35.35s %5d %6d %4d",
                name,
                e_ptr->counts.found, e_ptr->counts.bought, e_ptr->counts.destroyed
            );
            c_prt(attr, buf, 6 + i, max + 3);
        }
        /* Total Line? */
        if (i < browser_rows && ego_idx[ego_top + i] < 0)
        {
            char     buf[255];
            counts_t totals = {0};
            int j;
            for (j = 0; ego_idx[j] >= 0; j++)
            {
                ego_type *e_ptr = &e_info[ego_idx[j]];
                totals.found += e_ptr->counts.found;
                totals.bought += e_ptr->counts.bought;
                totals.destroyed += e_ptr->counts.destroyed;
            }

            sprintf(buf, "%35.35s %5d %6d %4d",
                "Totals",
                totals.found, totals.bought, totals.destroyed
            );
            c_prt(TERM_YELLOW, buf, 6 + i, max + 3);
            i++;
        }


        /* Clear remaining lines */
        for (; i < browser_rows; i++)
        {
            Term_erase(max + 3, 6 + i, 255);
        }

        prt("<dir>, 'r' to recall, ESC", hgt - 1, 0);

        if (!column)
        {
            Term_gotoxy(0, 6 + (grp_cur - grp_top));
        }
        else
        {
            Term_gotoxy(max + 3, 6 + (ego_cur - ego_top));
        }

        ch = inkey();

        switch (ch)
        {
        case ESCAPE:
            flag = TRUE;
            break;

        case 'R':
        case 'r':
        case 'I':
        case 'i':
            if (grp_cnt > 0 && ego_idx[ego_cur] >= 0)
            {
                desc_ego_fake(ego_idx[ego_cur]);
                redraw = TRUE;
            }
            break;

        default:
            browser_cursor(ch, &column, &grp_cur, grp_cnt, &ego_cur, ego_cnt);
        }
    }

    C_KILL(ego_idx, max_e_idx, int);
}


/*
 * Display known objects
 */
static void do_cmd_knowledge_objects(bool *need_redraw, bool visual_only, int direct_k_idx)
{
    int i, len, max;
    int grp_cur, grp_top, old_grp_cur;
    int object_old, object_cur, object_top;
    int grp_cnt, grp_idx[100];
    int object_cnt;
    int *object_idx;

    int column = 0;
    bool flag;
    bool redraw;

    bool visual_list = FALSE;
    byte attr_top = 0, char_left = 0;

    int browser_rows;
    int wid, hgt;

    byte mode;

    /* Get size */
    Term_get_size(&wid, &hgt);

    browser_rows = hgt - 8;

    /* Allocate the "object_idx" array */
    C_MAKE(object_idx, max_k_idx, int);

    max = 0;
    grp_cnt = 0;

    if (direct_k_idx < 0)
    {
        mode = visual_only ? 0x03 : 0x01;

        /* Check every group */
        for (i = 0; object_group_text[i] != NULL; i++)
        {
            /* Measure the label */
            len = strlen(object_group_text[i]);

            /* Save the maximum length */
            if (len > max) max = len;

            /* See if any monsters are known */
            if (collect_objects(i, object_idx, mode))
            {
                /* Build a list of groups with known monsters */
                grp_idx[grp_cnt++] = i;
            }
        }

        object_old = -1;
        object_cnt = 0;
    }
    else
    {
        object_kind *k_ptr = &k_info[direct_k_idx];
        object_kind *flavor_k_ptr;

        if (!visual_only && k_ptr->flavor)
        {
            /* Appearance of this object is shuffled */
            flavor_k_ptr = &k_info[k_ptr->flavor];
        }
        else
        {
            /* Appearance of this object is very normal */
            flavor_k_ptr = k_ptr;
        }

        object_idx[0] = direct_k_idx;
        object_old = direct_k_idx;
        object_cnt = 1;

        /* Terminate the list */
        object_idx[1] = -1;

        (void)visual_mode_command('v', &visual_list, browser_rows - 1, wid - (max + 3),
            &attr_top, &char_left, &flavor_k_ptr->x_attr, &flavor_k_ptr->x_char, need_redraw);
    }

    /* Terminate the list */
    grp_idx[grp_cnt] = -1;

    old_grp_cur = -1;
    grp_cur = grp_top = 0;
    object_cur = object_top = 0;

    flag = FALSE;
    redraw = TRUE;

    mode = visual_only ? 0x02 : 0x00;

    while (!flag)
    {
        char ch;
        object_kind *k_ptr = NULL, *flavor_k_ptr = NULL;

        if (redraw)
        {
            clear_from(0);

            prt(format("%s - Objects", !visual_only ? "Knowledge" : "Visuals"), 2, 0);
            if (direct_k_idx < 0) prt("Group", 4, 0);
            prt("Name", 4, max + 3);
            if (visual_only) prt("Idx", 4, 70);
            prt("Found Bought Used Dest Sym", 4, 52);

            for (i = 0; i < 78; i++)
            {
                Term_putch(i, 5, TERM_WHITE, '=');
            }

            if (direct_k_idx < 0)
            {
                for (i = 0; i < browser_rows; i++)
                {
                    Term_putch(max + 1, 6 + i, TERM_WHITE, '|');
                }
            }

            redraw = FALSE;
        }

        if (direct_k_idx < 0)
        {
            /* Scroll group list */
            if (grp_cur < grp_top) grp_top = grp_cur;
            if (grp_cur >= grp_top + browser_rows) grp_top = grp_cur - browser_rows + 1;

            /* Display a list of object groups */
            display_group_list(0, 6, max, browser_rows, grp_idx, object_group_text, grp_cur, grp_top);

            if (old_grp_cur != grp_cur)
            {
                old_grp_cur = grp_cur;

                /* Get a list of objects in the current group */
                object_cnt = collect_objects(grp_idx[grp_cur], object_idx, mode) + 1;
            }

            /* Scroll object list */
            while (object_cur < object_top)
                object_top = MAX(0, object_top - browser_rows/2);
            while (object_cur >= object_top + browser_rows)
                object_top = MIN(object_cnt - browser_rows, object_top + browser_rows/2);
        }

        if (!visual_list)
        {
            /* Display a list of objects in the current group */
            display_object_list(max + 3, 6, browser_rows, object_idx, object_cur, object_top, object_cnt, visual_only);
        }
        else
        {
            object_top = object_cur;

            /* Display a list of objects in the current group */
            display_object_list(max + 3, 6, 1, object_idx, object_cur, object_top, object_cnt, visual_only);

            /* Display visual list below first object */
            display_visual_list(max + 3, 7, browser_rows-1, wid - (max + 3), attr_top, char_left);
        }

        /* Get the current object */
        if (object_idx[object_cur] >= 0)
        {
            k_ptr = &k_info[object_idx[object_cur]];

            if (!visual_only && k_ptr->flavor)
            {
                /* Appearance of this object is shuffled */
                flavor_k_ptr = &k_info[k_ptr->flavor];
            }
            else
            {
                /* Appearance of this object is very normal */
                flavor_k_ptr = k_ptr;
            }
        }
        else
        {
            k_ptr = NULL;
            flavor_k_ptr = NULL;
        }

        /* Prompt */
        prt(format("<dir>%s%s%s, ESC",
            (!visual_list && !visual_only) ? ", 'r' to recall" : "",
            visual_list ? ", ENTER to accept" : ", 'v' for visuals",
            (attr_idx || char_idx) ? ", 'c', 'p' to paste" : ", 'c' to copy"),
            hgt - 1, 0);

        if (!visual_only && object_idx[object_cur] >= 0)
        {
            /* Mega Hack -- track this object */
            if (object_cnt)
                object_kind_track(object_idx[object_cur]);

            /* The "current" object changed */
            if (object_old != object_idx[object_cur])
            {
                /* Hack -- handle stuff */
                handle_stuff();

                /* Remember the "current" object */
                object_old = object_idx[object_cur];
            }
        }

        if (visual_list && flavor_k_ptr)
        {
            place_visual_list_cursor(max + 3, 7, flavor_k_ptr->x_attr, flavor_k_ptr->x_char, attr_top, char_left);
        }
        else if (!column)
        {
            Term_gotoxy(0, 6 + (grp_cur - grp_top));
        }
        else
        {
            Term_gotoxy(max + 3, 6 + (object_cur - object_top));
        }

        ch = inkey();

        /* Do visual mode command if needed */
        if (flavor_k_ptr && visual_mode_command(ch, &visual_list, browser_rows-1, wid - (max + 3), &attr_top, &char_left, &flavor_k_ptr->x_attr, &flavor_k_ptr->x_char, need_redraw))
        {
            if (direct_k_idx >= 0)
            {
                switch (ch)
                {
                case '\n':
                case '\r':
                case ESCAPE:
                    flag = TRUE;
                    break;
                }
            }
            continue;
        }

        switch (ch)
        {
            case ESCAPE:
            {
                flag = TRUE;
                break;
            }

            case 'R':
            case 'r':
            {
                /* Recall on screen */
                if (!visual_list && !visual_only && (grp_cnt > 0) && object_idx[object_cur] >= 0)
                {
                    desc_obj_fake(object_idx[object_cur]);
                    redraw = TRUE;
                }
                break;
            }

            default:
            {
                /* Move the cursor */
                browser_cursor(ch, &column, &grp_cur, grp_cnt, &object_cur, object_cnt);
                break;
            }
        }
    }

    /* Free the "object_idx" array */
    C_KILL(object_idx, max_k_idx, int);
}


/*
 * Display the features in a group.
 */
static void display_feature_list(int col, int row, int per_page, int *feat_idx,
    int feat_cur, int feat_top, bool visual_only, int lighting_level)
{
    int lit_col[F_LIT_MAX], i, j;
    int f_idx_col = use_bigtile ? 62 : 64;

    /* Correct columns 1 and 4 */
    lit_col[F_LIT_STANDARD] = use_bigtile ? (71 - F_LIT_MAX) : 71;
    for (i = F_LIT_NS_BEGIN; i < F_LIT_MAX; i++)
        lit_col[i] = lit_col[F_LIT_STANDARD] + 2 + (i - F_LIT_NS_BEGIN) * 2 + (use_bigtile ? i : 0);

    /* Display lines until done */
    for (i = 0; i < per_page && (feat_idx[feat_top + i] >= 0); i++)
    {
        byte attr;

        /* Get the index */
        int f_idx = feat_idx[feat_top + i];

        /* Access the index */
        feature_type *f_ptr = &f_info[f_idx];

        int row_i = row + i;

        /* Choose a color */
        attr = ((i + feat_top == feat_cur) ? TERM_L_BLUE : TERM_WHITE);

        /* Display the name */
        c_prt(attr, f_name + f_ptr->name, row_i, col);

        /* Hack -- visual_list mode */
        if (per_page == 1)
        {
            /* Display lighting level */
            c_prt(attr, format("(%s)", lighting_level_str[lighting_level]), row_i, col + 1 + strlen(f_name + f_ptr->name));

            c_prt(attr, format("%02x/%02x", f_ptr->x_attr[lighting_level], f_ptr->x_char[lighting_level]), row_i, f_idx_col - ((p_ptr->wizard || visual_only) ? 6 : 2));
        }
        if (p_ptr->wizard || visual_only)
        {
            c_prt(attr, format("%d", f_idx), row_i, f_idx_col);
        }

        /* Display symbol */
        Term_queue_bigchar(lit_col[F_LIT_STANDARD], row_i, f_ptr->x_attr[F_LIT_STANDARD], f_ptr->x_char[F_LIT_STANDARD], 0, 0);

        Term_putch(lit_col[F_LIT_NS_BEGIN], row_i, TERM_SLATE, '(');
        for (j = F_LIT_NS_BEGIN + 1; j < F_LIT_MAX; j++)
        {
            Term_putch(lit_col[j], row_i, TERM_SLATE, '/');
        }
        Term_putch(lit_col[F_LIT_MAX - 1] + (use_bigtile ? 3 : 2), row_i, TERM_SLATE, ')');

        /* Mega-hack -- Use non-standard colour */
        for (j = F_LIT_NS_BEGIN; j < F_LIT_MAX; j++)
        {
            Term_queue_bigchar(lit_col[j] + 1, row_i, f_ptr->x_attr[j], f_ptr->x_char[j], 0, 0);
        }
    }

    /* Clear remaining lines */
    for (; i < per_page; i++)
    {
        Term_erase(col, row + i, 255);
    }
}


/*
 * Interact with feature visuals.
 */
static void do_cmd_knowledge_features(bool *need_redraw, bool visual_only, int direct_f_idx, int *lighting_level)
{
    int i, len, max;
    int grp_cur, grp_top, old_grp_cur;
    int feat_cur, feat_top;
    int grp_cnt, grp_idx[100];
    int feat_cnt;
    int *feat_idx;

    int column = 0;
    bool flag;
    bool redraw;

    bool visual_list = FALSE;
    byte attr_top = 0, char_left = 0;

    int browser_rows;
    int wid, hgt;

    byte attr_old[F_LIT_MAX];
    byte char_old[F_LIT_MAX];
    byte *cur_attr_ptr, *cur_char_ptr;

    C_WIPE(attr_old, F_LIT_MAX, byte);
    C_WIPE(char_old, F_LIT_MAX, byte);

    /* Get size */
    Term_get_size(&wid, &hgt);

    browser_rows = hgt - 8;

    /* Allocate the "feat_idx" array */
    C_MAKE(feat_idx, max_f_idx, int);

    max = 0;
    grp_cnt = 0;

    if (direct_f_idx < 0)
    {
        /* Check every group */
        for (i = 0; feature_group_text[i] != NULL; i++)
        {
            /* Measure the label */
            len = strlen(feature_group_text[i]);

            /* Save the maximum length */
            if (len > max) max = len;

            /* See if any features are known */
            if (collect_features(i, feat_idx, 0x01))
            {
                /* Build a list of groups with known features */
                grp_idx[grp_cnt++] = i;
            }
        }

        feat_cnt = 0;
    }
    else
    {
        feature_type *f_ptr = &f_info[direct_f_idx];

        feat_idx[0] = direct_f_idx;
        feat_cnt = 1;

        /* Terminate the list */
        feat_idx[1] = -1;

        (void)visual_mode_command('v', &visual_list, browser_rows - 1, wid - (max + 3),
            &attr_top, &char_left, &f_ptr->x_attr[*lighting_level], &f_ptr->x_char[*lighting_level], need_redraw);

        for (i = 0; i < F_LIT_MAX; i++)
        {
            attr_old[i] = f_ptr->x_attr[i];
            char_old[i] = f_ptr->x_char[i];
        }
    }

    /* Terminate the list */
    grp_idx[grp_cnt] = -1;

    old_grp_cur = -1;
    grp_cur = grp_top = 0;
    feat_cur = feat_top = 0;

    flag = FALSE;
    redraw = TRUE;

    while (!flag)
    {
        char ch;
        feature_type *f_ptr;

        if (redraw)
        {
            clear_from(0);

            prt("Visuals - features", 2, 0);
            if (direct_f_idx < 0) prt("Group", 4, 0);
            prt("Name", 4, max + 3);
            if (use_bigtile)
            {
                if (p_ptr->wizard || visual_only) prt("Idx", 4, 62);
                prt("Sym ( l/ d)", 4, 67);
            }
            else
            {
                if (p_ptr->wizard || visual_only) prt("Idx", 4, 64);
                prt("Sym (l/d)", 4, 69);
            }

            for (i = 0; i < 78; i++)
            {
                Term_putch(i, 5, TERM_WHITE, '=');
            }

            if (direct_f_idx < 0)
            {
                for (i = 0; i < browser_rows; i++)
                {
                    Term_putch(max + 1, 6 + i, TERM_WHITE, '|');
                }
            }

            redraw = FALSE;
        }

        if (direct_f_idx < 0)
        {
            /* Scroll group list */
            if (grp_cur < grp_top) grp_top = grp_cur;
            if (grp_cur >= grp_top + browser_rows) grp_top = grp_cur - browser_rows + 1;

            /* Display a list of feature groups */
            display_group_list(0, 6, max, browser_rows, grp_idx, feature_group_text, grp_cur, grp_top);

            if (old_grp_cur != grp_cur)
            {
                old_grp_cur = grp_cur;

                /* Get a list of features in the current group */
                feat_cnt = collect_features(grp_idx[grp_cur], feat_idx, 0x00);
            }

            /* Scroll feature list */
            while (feat_cur < feat_top)
                feat_top = MAX(0, feat_top - browser_rows/2);
            while (feat_cur >= feat_top + browser_rows)
                feat_top = MIN(feat_cnt - browser_rows, feat_top + browser_rows/2);
        }

        if (!visual_list)
        {
            /* Display a list of features in the current group */
            display_feature_list(max + 3, 6, browser_rows, feat_idx, feat_cur, feat_top, visual_only, F_LIT_STANDARD);
        }
        else
        {
            feat_top = feat_cur;

            /* Display a list of features in the current group */
            display_feature_list(max + 3, 6, 1, feat_idx, feat_cur, feat_top, visual_only, *lighting_level);

            /* Display visual list below first object */
            display_visual_list(max + 3, 7, browser_rows-1, wid - (max + 3), attr_top, char_left);
        }

        /* Prompt */
        prt(format("<dir>%s, 'd' for default lighting%s, ESC",
            visual_list ? ", ENTER to accept, 'a' for lighting level" : ", 'v' for visuals",
            (attr_idx || char_idx) ? ", 'c', 'p' to paste" : ", 'c' to copy"),
            hgt - 1, 0);

        /* Get the current feature */
        f_ptr = &f_info[feat_idx[feat_cur]];
        cur_attr_ptr = &f_ptr->x_attr[*lighting_level];
        cur_char_ptr = &f_ptr->x_char[*lighting_level];

        if (visual_list)
        {
            place_visual_list_cursor(max + 3, 7, *cur_attr_ptr, *cur_char_ptr, attr_top, char_left);
        }
        else if (!column)
        {
            Term_gotoxy(0, 6 + (grp_cur - grp_top));
        }
        else
        {
            Term_gotoxy(max + 3, 6 + (feat_cur - feat_top));
        }

        ch = inkey();

        if (visual_list && ((ch == 'A') || (ch == 'a')))
        {
            int prev_lighting_level = *lighting_level;

            if (ch == 'A')
            {
                if (*lighting_level <= 0) *lighting_level = F_LIT_MAX - 1;
                else (*lighting_level)--;
            }
            else
            {
                if (*lighting_level >= F_LIT_MAX - 1) *lighting_level = 0;
                else (*lighting_level)++;
            }

            if (f_ptr->x_attr[prev_lighting_level] != f_ptr->x_attr[*lighting_level])
                attr_top = MAX(0, (f_ptr->x_attr[*lighting_level] & 0x7f) - 5);

            if (f_ptr->x_char[prev_lighting_level] != f_ptr->x_char[*lighting_level])
                char_left = MAX(0, f_ptr->x_char[*lighting_level] - 10);

            continue;
        }

        else if ((ch == 'D') || (ch == 'd'))
        {
            byte prev_x_attr = f_ptr->x_attr[*lighting_level];
            byte prev_x_char = f_ptr->x_char[*lighting_level];

            apply_default_feat_lighting(f_ptr->x_attr, f_ptr->x_char);

            if (visual_list)
            {
                if (prev_x_attr != f_ptr->x_attr[*lighting_level])
                     attr_top = MAX(0, (f_ptr->x_attr[*lighting_level] & 0x7f) - 5);

                if (prev_x_char != f_ptr->x_char[*lighting_level])
                    char_left = MAX(0, f_ptr->x_char[*lighting_level] - 10);
            }
            else *need_redraw = TRUE;

            continue;
        }

        /* Do visual mode command if needed */
        else if (visual_mode_command(ch, &visual_list, browser_rows-1, wid - (max + 3), &attr_top, &char_left, cur_attr_ptr, cur_char_ptr, need_redraw))
        {
            switch (ch)
            {
            /* Restore previous visual settings */
            case ESCAPE:
                for (i = 0; i < F_LIT_MAX; i++)
                {
                    f_ptr->x_attr[i] = attr_old[i];
                    f_ptr->x_char[i] = char_old[i];
                }

                /* Fall through */

            case '\n':
            case '\r':
                if (direct_f_idx >= 0) flag = TRUE;
                else *lighting_level = F_LIT_STANDARD;
                break;

            /* Preserve current visual settings */
            case 'V':
            case 'v':
                for (i = 0; i < F_LIT_MAX; i++)
                {
                    attr_old[i] = f_ptr->x_attr[i];
                    char_old[i] = f_ptr->x_char[i];
                }
                *lighting_level = F_LIT_STANDARD;
                break;

            case 'C':
            case 'c':
                if (!visual_list)
                {
                    for (i = 0; i < F_LIT_MAX; i++)
                    {
                        attr_idx_feat[i] = f_ptr->x_attr[i];
                        char_idx_feat[i] = f_ptr->x_char[i];
                    }
                }
                break;

            case 'P':
            case 'p':
                if (!visual_list)
                {
                    /* Allow TERM_DARK text */
                    for (i = F_LIT_NS_BEGIN; i < F_LIT_MAX; i++)
                    {
                        if (attr_idx_feat[i] || (!(char_idx_feat[i] & 0x80) && char_idx_feat[i])) f_ptr->x_attr[i] = attr_idx_feat[i];
                        if (char_idx_feat[i]) f_ptr->x_char[i] = char_idx_feat[i];
                    }
                }
                break;
            }
            continue;
        }

        switch (ch)
        {
            case ESCAPE:
            {
                flag = TRUE;
                break;
            }

            default:
            {
                /* Move the cursor */
                browser_cursor(ch, &column, &grp_cur, grp_cnt, &feat_cur, feat_cnt);
                break;
            }
        }
    }

    /* Free the "feat_idx" array */
    C_KILL(feat_idx, max_f_idx, int);
}


/*
 * List wanted monsters
 */
static void do_cmd_knowledge_kubi(void)
{
    int i;
    FILE *fff;

    char file_name[1024];


    /* Open a new file */
    fff = my_fopen_temp(file_name, 1024);
    if (!fff) {
        msg_format("Failed to create temporary file %s.", file_name);
        msg_print(NULL);
        return;
    }

    if (fff)
    {
        bool listed = FALSE;

        fprintf(fff, "Today target : %s\n", (p_ptr->today_mon ? r_name + r_info[p_ptr->today_mon].name : "unknown"));
        fprintf(fff, "\n");
        fprintf(fff, "List of wanted monsters\n");
        fprintf(fff, "----------------------------------------------\n");

        for (i = 0; i < MAX_KUBI; i++)
        {
            int id = kubi_r_idx[i];
            if (0 < id && id < 10000)
            {
                fprintf(fff,"%s\n", r_name + r_info[id].name);
                listed = TRUE;
            }
        }

        if (!listed)
        {
            fprintf(fff,"\n%s\n", "You have turned in all wanted monsters.");
        }
    }

    /* Close the file */
    my_fclose(fff);

    /* Display the file contents */
    show_file(TRUE, file_name, "Wanted monsters", 0, 0);


    /* Remove the file */
    fd_kill(file_name);
}

/*
 * List virtues & status
 */

static void do_cmd_knowledge_virtues(void)
{
    doc_ptr doc = doc_alloc(80);

    virtue_display(doc);
    doc_display(doc, "Virtues", 0);
    doc_free(doc);
}

/*
* Dungeon
*
*/
static void do_cmd_knowledge_dungeon(void)
{
    doc_ptr doc = doc_alloc(80);

    py_display_dungeons(doc);
    doc_display(doc, "Dungeons", 0);
    doc_free(doc);
}

static void do_cmd_knowledge_stat(void)
{
    doc_ptr          doc = doc_alloc(80);
    race_t          *race_ptr = get_race();
    class_t         *class_ptr = get_class();
    personality_ptr  pers_ptr = get_personality();
    int              i;

    if (p_ptr->knowledge & KNOW_HPRATE)
        doc_printf(doc, "Your current Life Rating is %s.\n\n", life_rating_desc(TRUE));
    else
        doc_insert(doc, "Your current Life Rating is <color:y>\?\?\?</color>.\n\n");

    doc_insert(doc, "<color:r>Limits of maximum stats</color>\n");

    for (i = 0; i < MAX_STATS; i++)
    {
        if ((p_ptr->knowledge & KNOW_STAT) || p_ptr->stat_max[i] == p_ptr->stat_max_max[i])
        {
            if (decimal_stats)
                doc_printf(doc, "%s <color:G>%d</color>\n", stat_names[i], (p_ptr->stat_max_max[i]-18)/10+18);
            else doc_printf(doc, "%s <color:G>18/%d</color>\n", stat_names[i], p_ptr->stat_max_max[i]-18);
        }
        else
            doc_printf(doc, "%s <color:y>\?\?\?</color>\n", stat_names[i]);
    }
    doc_insert(doc, "\n\n");

    doc_printf(doc, "<color:r>Race:</color> <color:B>%s</color>\n", race_ptr->name);
    doc_insert(doc, race_ptr->desc);
    if (p_ptr->pclass == CLASS_MONSTER)
        doc_printf(doc, " For more information, see <link:MonsterRaces.txt#%s>.\n\n", race_ptr->name);
    else
        doc_printf(doc, " For more information, see <link:Races.txt#%s>.\n\n", race_ptr->name);

    if (race_ptr->subdesc && strlen(race_ptr->subdesc))
    {
        doc_printf(doc, "<color:r>Subrace:</color> <color:B>%s</color>\n", race_ptr->subname);
        doc_insert(doc, race_ptr->subdesc);
        doc_insert(doc, "\n\n");
    }

    if (p_ptr->pclass != CLASS_MONSTER)
    {
        doc_printf(doc, "<color:r>Class:</color> <color:B>%s</color>\n", class_ptr->name);
        doc_insert(doc, class_ptr->desc);
        doc_printf(doc, " For more information, see <link:Classes.txt#%s>.\n\n", class_ptr->name);
    }

    doc_printf(doc, "<color:r>Personality:</color> <color:B>%s</color>\n", pers_ptr->name);
    doc_insert(doc, pers_ptr->desc);
    doc_printf(doc, " For more information, see <link:Personalities.txt#%s>.\n\n", pers_ptr->name);

    if (p_ptr->realm1)
    {
        doc_printf(doc, "<color:r>Realm:</color> <color:B>%s</color>\n", realm_names[p_ptr->realm1]);
        doc_insert(doc, realm_jouhou[technic2magic(p_ptr->realm1)-1]);
        doc_insert(doc, "\n\n");
    }

    if (p_ptr->realm2)
    {
        doc_printf(doc, "<color:r>Realm:</color> <color:B>%s</color>\n", realm_names[p_ptr->realm2]);
        doc_insert(doc, realm_jouhou[technic2magic(p_ptr->realm2)-1]);
        doc_insert(doc, "\n\n");
    }

    doc_display(doc, "Self Knowledge", 0);
    doc_free(doc);
}

/*
 * Check the status of "autopick"
 */
static void do_cmd_knowledge_autopick(void)
{
    int k;
    doc_ptr doc = doc_alloc(80);

    if (no_mogaminator)
    {
        doc_insert(doc, "You have disabled the Mogaminator.\n");
    }
    else if (!max_autopick)
    {
        doc_insert(doc, "You have not yet activated the Mogaminator.\n");
    }
    else if (max_autopick == 1)
    {
        doc_insert(doc, "There is 1 registered line for automatic object management.\n");
    }
    else
    {
        doc_printf(doc, "There are %d registered lines for automatic object management.\n", max_autopick);
    }
    doc_insert(doc, "For help on the Mogaminator, see <link:editor.txt>.\n\n");

    if (!no_mogaminator)
    {
        for (k = 0; k < max_autopick; k++)
        {
            cptr tmp;
            string_ptr line = 0;
            char color = 'w';
            byte act = autopick_list[k].action;
            if (act & DONT_AUTOPICK)
            {
                tmp = "Leave";
                color = 'U';
            }
            else if (act & DO_AUTODESTROY)
            {
                tmp = "Destroy";
                color = 'r';
            }
            else if (act & DO_AUTOPICK)
            {
                tmp = "Pick Up";
                color = 'B';
            }
            else /* if (act & DO_QUERY_AUTOPICK) */ /* Obvious */
            {
                tmp = "Query";
                color = 'y';
            }

            if (act & DO_DISPLAY)
                doc_printf(doc, "<color:%c>%-9.9s</color>", color, format("[%s]", tmp));
            else
                doc_printf(doc, "<color:%c>%-9.9s</color>", color, format("(%s)", tmp));

            line = autopick_line_from_entry(&autopick_list[k], AUTOPICK_COLOR_CODED);
            doc_printf(doc, " <indent><style:indent>%s</style></indent>\n", string_buffer(line));
            string_free(line);
        }
    }

    doc_display(doc, "Mogaminator Preferences", 0);
    doc_free(doc);
}


/*
 * Interact with "knowledge"
 */
void do_cmd_knowledge(void)
{
    int      i, row, col;
    bool     need_redraw = FALSE;
    class_t *class_ptr = get_class();
    race_t  *race_ptr = get_race();

    screen_save();

    while (1)
    {
        Term_clear();

        prt("Display current knowledge", 2, 0);

        /* Give some choices */
        row = 4;
        col = 2;
        c_prt(TERM_RED, "Object Knowledge", row++, col - 2);
        prt("(a) Artifacts", row++, col);
        prt("(o) Objects", row++, col);
        prt("(e) Egos", row++, col);
        prt("(_) Auto Pick/Destroy", row++, col);
        row++;

        c_prt(TERM_RED, "Monster Knowledge", row++, col - 2);
        prt("(m) Known Monsters", row++, col);
        prt("(w) Wanted Monsters", row++, col);
        prt("(u) Remaining Uniques", row++, col);
        prt("(k) Kill Count", row++, col);
        prt("(p) Pets", row++, col);
        row++;

        row = 4;
        col = 30;

        c_prt(TERM_RED, "Dungeon Knowledge", row++, col - 2);
        prt("(d) Dungeons", row++, col);
        prt("(q) Quests", row++, col);
        prt("(t) Terrain Symbols", row++, col);
        row++;

        c_prt(TERM_RED, "Self Knowledge", row++, col - 2);
        prt("(@) About Yourself", row++, col);
        if (p_ptr->prace != RACE_MON_RING)
            prt("(W) Weapon Damage", row++, col);
        if (equip_find_obj(TV_BOW, SV_ANY) && !prace_is_(RACE_MON_JELLY) && p_ptr->shooter_info.tval_ammo != TV_NO_AMMO)
            prt("(S) Shooter Damage", row++, col);
        if (mut_count(NULL))
            prt("(M) Mutations", row++, col);
        if (enable_virtues)
            prt("(v) Virtues", row++, col);
        if (class_ptr->character_dump || race_ptr->character_dump)
            prt("(x) Extra Info", row++, col);
        prt("(H) High Score List", row++, col);
        row++;

        c_prt(TERM_RED, "Skills", row++, col - 2);
        prt("(P) Proficiency", row++, col);
        if (p_ptr->pclass != CLASS_RAGE_MAGE) /* TODO */
            prt("(s) Spell Proficiency", row++, col);
        row++;

        /* Prompt */
        prt("ESC) Exit menu", 21, 1);
        prt("Command: ", 20, 0);

        /* Prompt */
        i = inkey();

        /* Done */
        if (i == ESCAPE) break;
        switch (i)
        {
        /* Object Knowledge */
        case 'a':
            do_cmd_knowledge_artifacts();
            break;
        case 'o':
            do_cmd_knowledge_objects(&need_redraw, FALSE, -1);
            break;
        case 'e':
            do_cmd_knowledge_egos();
            break;
        case '_':
            do_cmd_knowledge_autopick();
            break;

        /* Monster Knowledge */
        case 'm':
            do_cmd_knowledge_monsters(&need_redraw, FALSE, -1);
            break;
        case 'w':
            do_cmd_knowledge_kubi();
            break;
        case 'u':
            do_cmd_knowledge_uniques();
            break;
        case 'k':
            do_cmd_knowledge_kill_count();
            break;
        case 'p':
            do_cmd_knowledge_pets();
            break;

        /* Dungeon Knowledge */
        case 'd':
            do_cmd_knowledge_dungeon();
            break;
        case 'q':
            quests_display();
            break;
        case 't':
            {
                int lighting_level = F_LIT_STANDARD;
                do_cmd_knowledge_features(&need_redraw, FALSE, -1, &lighting_level);
            }
            break;

        /* Self Knowledge */
        case '@':
            do_cmd_knowledge_stat();
            break;
        case 'W':
            if (p_ptr->prace != RACE_MON_RING)
                do_cmd_knowledge_weapon();
            else
                bell();
            break;
        case 'S':
            if (equip_find_obj(TV_BOW, SV_ANY) && !prace_is_(RACE_MON_JELLY) && p_ptr->shooter_info.tval_ammo != TV_NO_AMMO)
                do_cmd_knowledge_shooter();
            else
                bell();
            break;
        case 'M':
            if (mut_count(NULL))
                mut_do_cmd_knowledge();
            else
                bell();
            break;
        case 'v':
            if (enable_virtues)
                do_cmd_knowledge_virtues();
            else
                bell();
            break;
        case 'x':
            if (class_ptr->character_dump || race_ptr->character_dump)
                do_cmd_knowledge_extra();
            else
                bell();
            break;
        case 'H': {
            vec_ptr scores;
            if (check_score())
                scores_update();
            scores = scores_load(NULL);
            scores_display(scores);
            vec_free(scores);
            break; }

        /* Skills */
        case 'P':
            do_cmd_knowledge_weapon_exp();
            break;
        case 's':
            if (p_ptr->pclass != CLASS_RAGE_MAGE)  /* TODO */
                do_cmd_knowledge_spell_exp();
            break;

        default:
            bell();
        }

        /* Flush messages */
        msg_print(NULL);
    }

    /* Restore the screen */
    screen_load();

    if (need_redraw) do_cmd_redraw();
}

/*
 * Display the time and date
 */
void do_cmd_time(void)
{
    int day, hour, min, full, start, end, num;
    char desc[1024];

    char buf[1024];
    char day_buf[10];

    FILE *fff;

    extract_day_hour_min(&day, &hour, &min);

    full = hour * 100 + min;

    start = 9999;
    end = -9999;

    num = 0;

    strcpy(desc, "It is a strange time.");


    if (day < MAX_DAYS) sprintf(day_buf, "%d", day);
    else strcpy(day_buf, "*****");

    /* Message */
    msg_format("This is day %s. The time is %d:%02d %s.",
           day_buf, (hour % 12 == 0) ? 12 : (hour % 12),
           min, (hour < 12) ? "AM" : "PM");


    /* Find the path */
    if (!randint0(10) || p_ptr->image)
    {
        path_build(buf, sizeof(buf), ANGBAND_DIR_FILE, "timefun.txt");

    }
    else
    {
        path_build(buf, sizeof(buf), ANGBAND_DIR_FILE, "timenorm.txt");

    }

    /* Open this file */
    fff = my_fopen(buf, "rt");

    /* Oops */
    if (!fff) return;

    /* Find this time */
    while (!my_fgets(fff, buf, sizeof(buf)))
    {
        /* Ignore comments */
        if (!buf[0] || (buf[0] == '#')) continue;

        /* Ignore invalid lines */
        if (buf[1] != ':') continue;

        /* Process 'Start' */
        if (buf[0] == 'S')
        {
            /* Extract the starting time */
            start = atoi(buf + 2);

            /* Assume valid for an hour */
            end = start + 59;

            /* Next... */
            continue;
        }

        /* Process 'End' */
        if (buf[0] == 'E')
        {
            /* Extract the ending time */
            end = atoi(buf + 2);

            /* Next... */
            continue;
        }

        /* Ignore incorrect range */
        if ((start > full) || (full > end)) continue;

        /* Process 'Description' */
        if (buf[0] == 'D')
        {
            num++;

            /* Apply the randomizer */
            if (!randint0(num)) strcpy(desc, buf + 2);

            /* Next... */
            continue;
        }
    }

    if (p_ptr->prace == RACE_WEREWOLF)
    {
        strcat(desc, werewolf_moon_message());
    }

    /* Message */
    msg_print(desc);

    /* Close the file */
    my_fclose(fff);
}
