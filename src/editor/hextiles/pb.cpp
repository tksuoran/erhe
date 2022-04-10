#include "game.hpp"
#include "texture_util.hpp"

int main(
    int argc,
    const char * const * const argv
)
{
    static_cast<void>(argc);
    static_cast<void>(argv);

    //apply_mask();

    {
        power_battle::Game game;
        game.init();
    }

	return 0;
}
