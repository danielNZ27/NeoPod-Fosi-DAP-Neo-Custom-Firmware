#include "browse_screen.h"
#include "check.h"
#include "default_theme.h"
#include "fixture_library.h"
#include "big_library.h"

static FixtureLibrary fixture;
static BrowsePool pool;

static BrowseScreen *opened(BrowseQuery query)
{
    browse_pool_init(&pool, (BrowsePoolConfig){.library = fixture_library_open(&fixture),
                                               .theme = &DEFAULT_THEME});
    Screen *screen = browse_pool_open(&pool, 0, query);
    screen->enter(screen->context, 0);
    return screen->context;
}

static int section_at(const BrowseScreen *browse, int index)
{
    return browse->model.section(browse->model.context, index);
}

static void albums_are_lettered_by_their_sort_key(void)
{
    BrowseScreen *albums = opened(browse_root_query(BROWSE_ALBUMS));
    CHECK(albums->model.section != NULL);
    CHECK_EQUAL(LIST_SECTION_FIRST_LETTER + 'b' - 'a', section_at(albums, 0));
    CHECK_EQUAL(LIST_SECTION_FIRST_LETTER + 'b' - 'a', section_at(albums, 1));
    CHECK_EQUAL(LIST_SECTION_FIRST_LETTER + 'h' - 'a', section_at(albums, 2));
    CHECK_EQUAL(LIST_SECTION_FIRST_LETTER + 't' - 'a', section_at(albums, 4));
}

static void lettered_lists_never_go_backwards(void)
{
    static const BrowseKind LETTERED[] = {BROWSE_ARTISTS, BROWSE_ALBUM_ARTISTS, BROWSE_GENRES,
                                          BROWSE_ALBUMS, BROWSE_ALL_SONGS};
    for (size_t kind = 0; kind < sizeof LETTERED / sizeof LETTERED[0]; kind++) {
        BrowseScreen *browse = opened(browse_root_query(LETTERED[kind]));
        CHECK(browse->model.section != NULL);
        for (int index = 1; index < (int)browse->count; index++) {
            CHECK(section_at(browse, index - 1) <= section_at(browse, index));
        }
    }
}

static void album_tracks_have_no_letters(void)
{
    BrowseQuery tracks = {.kind = BROWSE_ALBUM_TRACKS,
                          .group = LIBRARY_ARTISTS,
                          .subject = 0,
                          .artist = LIBRARY_NONE};
    CHECK(opened(tracks)->model.section == NULL);
}

static void portrait_lists_cache_all_twelve_rows_while_scrolling(void)
{
    BigLibrary big = {0};
    const Library *library = big_library_open(&big, 32, 1);
    const BrowseKind kinds[] = {BROWSE_ALL_SONGS, BROWSE_FOLDER, BROWSE_ALBUMS, BROWSE_ARTISTS};
    for (uint8_t theme = 0; theme < 2; theme++) {
        app_theme_select(theme, 0);
        for (size_t kind = 0; kind < sizeof kinds / sizeof kinds[0]; kind++) {
            browse_pool_init(&pool, (BrowsePoolConfig){.library = library, .theme = &APP_THEME});
            Screen *screen = browse_pool_open(&pool, 0, browse_root_query(kinds[kind]));
            screen->enter(screen->context, 0);
            BrowseScreen *browse = screen->context;
            CHECK_EQUAL(12, browse->list.visible_rows);
            CHECK(browse->count >= 12);
            for (int selected = 0; selected < (int)browse->count; selected++) {
                screen->update(screen->context, (uint32_t)selected);
                for (int row = 0; row < 12 && browse->list.top + row < (int)browse->count; row++) {
                    const char *label = browse->model.label(browse->model.context, browse->list.top + row);
                    CHECK(label[0] != '\0');
                }
                CHECK(browse->list.selected >= browse->list.top);
                CHECK(browse->list.selected < browse->list.top + 12);
                if (selected + 1 < (int)browse->count) {
                    screen->handle_key(screen->context,
                        (KeyEvent){KEY_WHEEL_DOWN, KEY_GESTURE_PRESS}, (uint32_t)selected + 1);
                }
            }
        }
    }
    app_theme_select(0, 0);
    big_library_close(&big);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(portrait_lists_cache_all_twelve_rows_while_scrolling),
        TEST_CASE(albums_are_lettered_by_their_sort_key),
        TEST_CASE(lettered_lists_never_go_backwards),
        TEST_CASE(album_tracks_have_no_letters),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
