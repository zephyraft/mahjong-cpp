#include "bitutils.hpp"

namespace mahjong
{
/**
 * @brief マスク
 */
const std::vector<int> Bit::mask = {
    7, 7 << 3, 7 << 6, 7 << 9, 7 << 12, 7 << 15, 7 << 18, 7 << 21, 7 << 24,
};

/**
 * @brief 1枚の牌
 */
const std::vector<int> Bit::hai1 = {
    1, 1 << 3, 1 << 6, 1 << 9, 1 << 12, 1 << 15, 1 << 18, 1 << 21, 1 << 24,
};

/**
 * @brief 2枚の牌
 */
const std::vector<int> Bit::hai2 = {
    2, 2 << 3, 2 << 6, 2 << 9, 2 << 12, 2 << 15, 2 << 18, 2 << 21, 2 << 24,
};

/**
 * @brief 3枚の牌
 */
const std::vector<int> Bit::hai3 = {
    3, 3 << 3, 3 << 6, 3 << 9, 3 << 12, 3 << 15, 3 << 18, 3 << 21, 3 << 24,
};

/**
 * @brief 4枚の牌
 */
const std::vector<int> Bit::hai4 = {
    4, 4 << 3, 4 << 6, 4 << 9, 4 << 12, 4 << 15, 4 << 18, 4 << 21, 4 << 24,
};

/**
 * @brief 2枚以上かどうかを調べるときに使うマスク
 */
const std::vector<int> Bit::ge2 = {
    6, 6 << 3, 6 << 6, 6 << 9, 6 << 12, 6 << 15, 6 << 18, 6 << 21, 6 << 24,
};

} // namespace mahjong
