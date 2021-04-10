#include "expectedvaluecalculator.hpp"

#undef NDEBUG
#include <algorithm>
#include <assert.h>
#include <fstream>
#include <numeric>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/dll.hpp>

#include "requiredtileselector.hpp"
#include "syanten.hpp"
#include "unnecessarytileselector.hpp"
#include "utils.hpp"

namespace mahjong
{

ExpectedValueCalculator::ExpectedValueCalculator()
    : calc_syanten_down_(false)
    , calc_tegawari_(false)
    , calc_double_reach_(false)
    , calc_ippatu_(false)
    , calc_haitei_(false)
    , calc_uradora_(false)
    , discard_cache_(5)      // 0(聴牌) ~ 4(4向聴)
    , draw_cache_(5)         // 0(聴牌) ~ 4(4向聴)
    , discard_node_cache_(5) // 0(聴牌) ~ 4(4向聴)
    , draw_node_cache_(5)    // 0(聴牌) ~ 4(4向聴)
{
    make_uradora_table();
}

/**
 * @brief 初期化する。
 *
 * @return 初期化に成功した場合は true、そうでない場合は false を返す。
 */
bool ExpectedValueCalculator::make_uradora_table()
{
    if (!uradora_prob_.empty())
        return true;

    uradora_prob_.resize(6);

    boost::filesystem::path path = boost::dll::program_location().parent_path() / "uradora.txt";
    std::ifstream ifs(path.string());

    std::string line;
    int i = 0;
    while (std::getline(ifs, line)) {
        std::vector<std::string> tokens;
        boost::split(tokens, line, boost::is_any_of(" "));

        for (auto token : tokens)
            uradora_prob_[i].push_back(std::stod(token));
        i++;
    }

    return true;
}
/**
 * @brief テーブルを初期化する。
 * 
 * @param[in] n_left_tiles 1巡目時点の残り枚数
 */
void ExpectedValueCalculator::create_prob_table(int n_left_tiles)
{
    // 有効牌の枚数ごとに、この巡目で有効牌を引ける確率のテーブルを作成する
    // tumo_probs_table_[i][j] = 有効牌の枚数が i 枚の場合に j 巡目に有効牌が引ける確率
    tumo_probs_table_.resize(5, std::vector<double>(17));
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 17; ++j)
            tumo_probs_table_[i][j] = double(i) / double(n_left_tiles - j);
    }

    // 有効牌の合計枚数ごとに、これまでの巡目で有効牌が引けなかった確率のテーブルを作成する
    // not_tumo_probs_table_[i][j] = 有効牌の合計枚数が i 枚の場合に j - 1 巡目までに有効牌が引けなかった確率
    not_tumo_probs_table_.resize(n_left_tiles, std::vector<double>(17));
    for (int i = 0; i < n_left_tiles; ++i) {
        not_tumo_probs_table_[i][0] = 1;
        for (int j = 1; j < 17; ++j) {
            not_tumo_probs_table_[i][j] = not_tumo_probs_table_[i][j - 1] *
                                          double(n_left_tiles - i - (j - 1)) /
                                          double(n_left_tiles - (j - 1));
        }
    }
}

/**
 * @brief キャッシュをクリアする。
 */
void ExpectedValueCalculator::clear_cache()
{
    // デバッグ用
    // spdlog::info("点数: {}", score_cache_.size());
    // for (size_t i = 0; i < 5; ++i)
    //     spdlog::info("向聴数{} 打牌候補: {}, 自摸候補: {} 打牌: {}, 自摸: {}", i,
    //                  discard_cache_[i].size(), draw_cache_[i].size(), discard_node_cache_[i].size(),
    //                  draw_node_cache_[i].size());

    std::for_each(discard_cache_.begin(), discard_cache_.end(), [](auto &x) { x.clear(); });
    std::for_each(draw_cache_.begin(), draw_cache_.end(), [](auto &x) { x.clear(); });
    std::for_each(discard_node_cache_.begin(), discard_node_cache_.end(),
                  [](auto &x) { x.clear(); });
    std::for_each(draw_node_cache_.begin(), draw_node_cache_.end(), [](auto &x) { x.clear(); });
    score_cache_.clear();
}

/**
 * @brief 期待値を計算する。
 * @param[in] hand 手牌
 * @param[in] score 点数計算機
 * @param[in] syanten_type 向聴数の種類
 * @param[in] n_extra_tumo 交換枚数 (手変わりまたは向聴戻しを行える回数)
 * @return 各打牌の情報
 */
std::tuple<bool, std::vector<Candidate>>
ExpectedValueCalculator::calc(const Hand &hand, const ScoreCalculator &score,
                              const std::vector<int> &dora_indicators, int syanten_type, int flag)
{
    score_ = score;
    syanten_type_ = syanten_type;
    calc_syanten_down_ = flag & CalcSyantenDown;
    calc_tegawari_ = flag & CalcTegawari;
    calc_double_reach_ = flag & CalcDoubleReach;
    calc_ippatu_ = flag & CalcIppatu;
    calc_haitei_ = flag & CalcHaiteitumo;
    calc_uradora_ = flag & CalcUradora;
    maximize_win_prob_ = flag & MaximaizeWinProb;
    dora_indicators_ = dora_indicators;

    // 手牌の枚数を数える。
    int n_tiles = hand.num_tiles() + int(hand.melds.size()) * 3;
    if (n_tiles != 14)
        return {false, {}}; // 手牌が14枚ではない

    // 現在の向聴数を計算する。
    auto [_, syanten] = SyantenCalculator::calc(hand, syanten_type_);
    if (syanten == -1)
        return {false, {}}; // 手牌が和了形または4向聴以上

    // 残り牌の枚数を数える。
    std::vector<int> counts = count_left_tiles(hand, dora_indicators_);
    int sum_left_tiles = std::accumulate(counts.begin(), counts.begin() + 34, 0);

    // 自摸確率のテーブルを作成する。
    create_prob_table(sum_left_tiles);

    std::vector<Candidate> candidates;
    if (syanten > 3) // 3向聴以下は聴牌確率、和了確率、期待値を計算する。
        candidates = analyze(syanten, hand);
    else // 4向聴以上は受け入れ枚数のみ計算する。
        candidates = analyze(0, syanten, hand);

    // キャッシュをクリアする。
    clear_cache();

    return {true, candidates};
}

/**
 * @brief
 *
 * @param[in] hand 手牌
 * @param[in] syanten_type 向聴数の種類
 * @param[in] counts 各牌の残り枚数
 * @return (有効牌の合計枚数, 有効牌の一覧)
 */
std::vector<std::tuple<int, int>>
ExpectedValueCalculator::get_required_tiles(const Hand &hand, int syanten_type,
                                            const std::vector<int> &counts)
{
    // 有効牌の一覧を取得する。
    std::vector<int> tiles = RequiredTileSelector::select(hand, syanten_type);

    std::vector<std::tuple<int, int>> required_tiles;
    for (auto tile : tiles)
        required_tiles.emplace_back(tile, counts[tile]);

    return required_tiles;
}

/**
 * @brief 自摸する。(手変わりを考慮しない)
 * 
 * @param[in] n_extra_tumo 
 * @param[in] syanten 向聴数
 * @param[in] hand 手牌
 * @param[in] counts 各牌の残り枚数
 * @return (各巡目の聴牌確率, 各巡目の和了確率, 各巡目の期待値)
 */
std::tuple<std::vector<double>, std::vector<double>, std::vector<double>>
ExpectedValueCalculator::draw_without_tegawari(int n_extra_tumo, int syanten, Hand &hand,
                                               std::vector<int> &counts)
{
    std::vector<double> tenpai_probs(17, 0), win_probs(17, 0), exp_values(17, 0);

    auto &table = draw_node_cache_[syanten];

    CacheKey key(hand, counts, n_extra_tumo);
    if (auto itr = table.find(key); itr != table.end())
        return itr->second; // キャッシュが存在する場合

        // 自摸候補を取得する。
#ifdef ENABLE_DRAW_TILES_CACHE
    const std::vector<int> &flags = get_draw_tiles(hand, syanten, counts);
#else
    std::vector<int> flags = get_draw_tiles(hand, syanten, counts);
#endif

    int sum_required_tiles = 0;
    for (int tile = 0; tile < 34; ++tile) {
        if (counts[tile] > 0 && flags[tile] == -1)
            sum_required_tiles += counts[tile];
    }

    for (int tile = 0; tile < 34; ++tile) {
        if (counts[tile] == 0 || flags[tile] != -1)
            continue;

        const std::vector<double> &tumo_probs = tumo_probs_table_[counts[tile]];
        const std::vector<double> &not_tumo_probs = not_tumo_probs_table_[sum_required_tiles];

        // 手牌に加える
        add_tile(hand, tile);
        counts[tile]--;

        std::vector<double> next_tenpai_probs, next_win_probs, next_exp_values;
        std::vector<double> scores;
        if (syanten == 0) {
            const ScoreCache &cache = get_score(hand, tile, counts);
            scores = cache.scores;
        }
        else {
            std::tie(next_tenpai_probs, next_win_probs, next_exp_values) =
                discard(n_extra_tumo, syanten - 1, hand, counts);
        }

        for (int i = 0; i < 17; ++i) {
            for (int j = i; j < 17; ++j) {
                // 現在の巡目が i の場合に j 巡目に有効牌を引く確率
                double prob = tumo_probs[j] * not_tumo_probs[j] / not_tumo_probs[i];

                if (syanten == 1) // 1向聴の場合は次で聴牌
                    tenpai_probs[i] += prob;
                else if (j < 16 && syanten > 1)
                    tenpai_probs[i] += prob * next_tenpai_probs[j + 1];

                // scores[0] == 0 の場合は役なしなので、和了確率、期待値は0
                if (syanten == 0 && scores[0] != 0) { // 聴牌の場合は次で和了
                    // i 巡目で聴牌の場合はダブル立直成立
                    bool win_double_reach = i == 0 && calc_double_reach_;
                    // i 巡目で聴牌し、次の巡目で和了の場合は一発成立
                    bool win_ippatu = j == i && calc_ippatu_;
                    // 最後の巡目で和了の場合は海底撈月成立
                    bool win_haitei = j == 16 && calc_haitei_;

                    win_probs[i] += prob;
                    exp_values[i] += prob * scores[win_double_reach + win_ippatu + win_haitei];
                }
                else if (j < 16 && syanten > 0) {
                    win_probs[i] += prob * next_win_probs[j + 1];
                    exp_values[i] += prob * next_exp_values[j + 1];
                }
            }
        }

        // 手牌から除く
        counts[tile]++;
        remove_tile(hand, tile);
    }

    auto [itr, _] = table.try_emplace(key, tenpai_probs, win_probs, exp_values);

    return itr->second;
}

/**
 * @brief 自摸する。(手変わりを考慮する)
 * 
 * @param[in] n_extra_tumo 
 * @param[in] syanten 向聴数
 * @param[in] hand 手牌
 * @param[in] counts 各牌の残り枚数
 * @return (各巡目の聴牌確率, 各巡目の和了確率, 各巡目の期待値)
 * 
 * この関数が呼ばれた時点で向聴戻しは行われていない
 */
std::tuple<std::vector<double>, std::vector<double>, std::vector<double>>
ExpectedValueCalculator::draw_with_tegawari(int n_extra_tumo, int syanten, Hand &hand,
                                            std::vector<int> &counts)
{
    std::vector<double> tenpai_probs(17, 0), win_probs(17, 0), exp_values(17, 0);

    auto &table = draw_node_cache_[syanten];

    CacheKey key(hand, counts, n_extra_tumo);
    if (auto itr = table.find(key); itr != table.end())
        return itr->second; // キャッシュが存在する場合

        // 自摸候補を取得する。
#ifdef ENABLE_DRAW_TILES_CACHE
    const std::vector<int> &flags = get_draw_tiles(hand, syanten, counts);
#else
    std::vector<int> flags = get_draw_tiles(hand, syanten, counts);
#endif

    for (int tile = 0; tile < 34; ++tile) {
        if (counts[tile] == 0 || flags[tile] != -1)
            continue;

        const std::vector<double> &tumo_probs = tumo_probs_table_[counts[tile]];

        // 手牌に加える
        add_tile(hand, tile);
        counts[tile]--;

        std::vector<double> next_tenpai_probs, next_win_probs, next_exp_values;
        std::vector<double> scores;

        if (syanten == 0) {
            const ScoreCache &cache = get_score(hand, tile, counts);
            scores = cache.scores;
        }
        else {
            std::tie(next_tenpai_probs, next_win_probs, next_exp_values) =
                discard(n_extra_tumo, syanten - 1, hand, counts);
        }

        for (int i = 0; i < 17; ++i) {
            if (syanten == 1) // 1向聴の場合は次で聴牌
                tenpai_probs[i] += tumo_probs[i];
            else if (i < 16 && syanten > 1)
                tenpai_probs[i] += tumo_probs[i] * next_tenpai_probs[i + 1];

            if (syanten == 0) { // 聴牌の場合は次で和了
                // i 巡目で聴牌の場合はダブル立直成立
                bool win_double_reach = i == 0 && calc_double_reach_;
                // i 巡目で聴牌し、次の巡目で和了の場合は一発成立
                bool win_ippatu = calc_ippatu_;
                // 最後の巡目で和了の場合は海底撈月成立
                bool win_haitei = i == 16 && calc_haitei_;

                win_probs[i] += tumo_probs[i];
                exp_values[i] += tumo_probs[i] * scores[win_double_reach + win_ippatu + win_haitei];
            }
            else if (i < 16 && syanten > 0) {
                win_probs[i] += tumo_probs[i] * next_win_probs[i + 1];
                exp_values[i] += tumo_probs[i] * next_exp_values[i + 1];
            }
        }

        // 手牌から除く
        counts[tile]++;
        remove_tile(hand, tile);
    }

    for (int tile = 0; tile < 34; ++tile) {
        if (counts[tile] == 0 || flags[tile] != 0)
            continue;

        const std::vector<double> &tumo_probs = tumo_probs_table_[counts[tile]];

        // 手牌に加える
        add_tile(hand, tile);
        counts[tile]--;

        auto [next_tenpai_probs, next_win_probs, next_exp_values] =
            discard(n_extra_tumo + 1, syanten, hand, counts);

        for (int i = 0; i < 16; ++i) {
            tenpai_probs[i] += tumo_probs[i] * next_tenpai_probs[i + 1];
            win_probs[i] += tumo_probs[i] * next_win_probs[i + 1];
            exp_values[i] += tumo_probs[i] * next_exp_values[i + 1];
        }

        // 手牌から除く
        counts[tile]++;
        remove_tile(hand, tile);
    }

    auto [itr, _] = table.try_emplace(key, tenpai_probs, win_probs, exp_values);

    return itr->second;
}

/**
 * @brief 自摸する。
 * 
 * @param[in] n_extra_tumo 
 * @param[in] syanten 向聴数
 * @param[in] hand 手牌
 * @param[in] counts 各牌の残り枚数
 * @return (各巡目の聴牌確率, 各巡目の和了確率, 各巡目の期待値)
 */
std::tuple<std::vector<double>, std::vector<double>, std::vector<double>>
ExpectedValueCalculator::draw(int n_extra_tumo, int syanten, Hand &hand, std::vector<int> &counts)
{
    if (calc_tegawari_ && n_extra_tumo == 0)
        return draw_with_tegawari(n_extra_tumo, syanten, hand, counts);
    else
        return draw_without_tegawari(n_extra_tumo, syanten, hand, counts);
}

/**
 * @brief 打牌する。
 * 
 * @param[in] n_extra_tumo 
 * @param[in] syanten 向聴数
 * @param[in] hand 手牌
 * @param[in] counts 各牌の残り枚数
 * @return (各巡目の聴牌確率, 各巡目の和了確率, 各巡目の期待値)
 */
std::tuple<std::vector<double>, std::vector<double>, std::vector<double>>
ExpectedValueCalculator::discard(int n_extra_tumo, int syanten, Hand &hand,
                                 std::vector<int> &counts)
{
    auto &table = discard_node_cache_[syanten];

    CacheKey key(hand, counts, n_extra_tumo);
    if (auto itr = table.find(key); itr != table.end())
        return itr->second; // キャッシュが存在する場合

        // 打牌候補を取得する。
#ifdef ENABLE_DISCARD_TILES_CACHE
    const std::vector<int> &flags = get_discard_tiles(hand, syanten);
#else
    const std::vector<int> flags = get_discard_tiles(hand, syanten);
#endif

    // 期待値が最大となる打牌を選択する。
    std::vector<double> max_tenpai_probs, max_win_probs, max_exp_values;
    std::vector<double> tenpai_probs, win_probs, exp_values;
    int max_tile = -1;
    double max_value = -1;
    for (int tile = 0; tile < 34; ++tile) {
        if (flags[tile] == 0) {
            // 向聴数が変化しない打牌
            remove_tile(hand, tile);
            std::tie(tenpai_probs, win_probs, exp_values) =
                draw(n_extra_tumo, syanten, hand, counts);
            add_tile(hand, tile);
        }
        else if (calc_syanten_down_ && n_extra_tumo == 0 && flags[tile] == 1) {
            // 向聴戻しになる打牌
            remove_tile(hand, tile);
            std::tie(tenpai_probs, win_probs, exp_values) =
                draw(n_extra_tumo + 1, syanten + 1, hand, counts);
            add_tile(hand, tile);
        }
        else {
            // 手牌に存在しない牌、または向聴落としが無効な場合に向聴落としとなる牌
            continue;
        }

        // 向聴戻し、手変わりは巡目がずれるので、1つ手前にずらす。(このやり方で正しいのか要検証)

        // 和了確率は下2桁まで一致していれば同じ、期待値は下0桁まで一致していれば同じとみなす。
        double value = maximize_win_prob_ ? int(win_probs[n_extra_tumo] * 10000)
                                          : int(exp_values[n_extra_tumo]);
        if ((value == max_value && DiscardPriorities[max_tile] < DiscardPriorities[tile]) ||
            value > max_value) {
            // 値が同等なら、DiscardPriorities が高い牌を優先して選択する。
            max_tenpai_probs = tenpai_probs;
            max_win_probs = win_probs;
            max_exp_values = exp_values;

            max_value = value;
            max_tile = tile;
        }
    }

    auto [itr, _] = table.try_emplace(key, max_tenpai_probs, max_win_probs, max_exp_values);

    return itr->second;
}

std::vector<Candidate> ExpectedValueCalculator::analyze(int n_extra_tumo, int syanten,
                                                        const Hand &_hand)
{
    std::vector<Candidate> candidates;
    Hand hand = _hand;

    // 各牌の残り枚数を数える。
    std::vector<int> counts = count_left_tiles(hand, dora_indicators_);

    // 打牌候補を取得する。
#ifdef ENABLE_DISCARD_TILES_CACHE
    const std::vector<int> &flags = get_discard_tiles(hand, syanten);
#else
    const std::vector<int> flags = get_discard_tiles(hand, syanten);
#endif

    for (int tile = 0; tile < 34; ++tile) {
        int discard_tile = tile;
        if (tile == Tile::Manzu5 && hand.aka_manzu5 && hand.num_tiles(Tile::Manzu5) == 1)
            discard_tile = Tile::AkaManzu5;
        else if (tile == Tile::Pinzu5 && hand.aka_pinzu5 && hand.num_tiles(Tile::Pinzu5) == 1)
            discard_tile = Tile::AkaPinzu5;
        else if (tile == Tile::Sozu5 && hand.aka_sozu5 && hand.num_tiles(Tile::Sozu5) == 1)
            discard_tile = Tile::AkaSozu5;

        if (flags[tile] == 0) {
            remove_tile(hand, tile);

            auto required_tiles = get_required_tiles(hand, syanten_type_, counts);

            auto [tenpai_probs, win_probs, exp_values] = draw(n_extra_tumo, syanten, hand, counts);

            add_tile(hand, tile);

            if (syanten == 0) // すでに聴牌している場合の例外処理
                std::fill(tenpai_probs.begin(), tenpai_probs.end(), 1);

            candidates.emplace_back(discard_tile, required_tiles, tenpai_probs, win_probs,
                                    exp_values, false);
        }
        else if (calc_syanten_down_ && flags[tile] == 1 && n_extra_tumo == 0) {
            remove_tile(hand, tile);

            auto required_tiles = get_required_tiles(hand, syanten_type_, counts);

            auto [tenpai_probs, win_probs, exp_values] =
                draw(n_extra_tumo + 1, syanten + 1, hand, counts);

            add_tile(hand, tile);

            // 向聴戻しは巡目がずれるので、1つ手前にずらす。(このやり方で正しいのか要検証)
            std::rotate(tenpai_probs.begin(), tenpai_probs.begin() + 1, tenpai_probs.end());
            tenpai_probs.back() = 0;
            std::rotate(win_probs.begin(), win_probs.begin() + 1, win_probs.end());
            win_probs.back() = 0;
            std::rotate(exp_values.begin(), exp_values.begin() + 1, exp_values.end());
            exp_values.back() = 0;

            candidates.emplace_back(discard_tile, required_tiles, tenpai_probs, win_probs,
                                    exp_values, true);
        }
    }

    return candidates;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

std::vector<Candidate> ExpectedValueCalculator::analyze(int syanten, const Hand &_hand)
{
    std::vector<Candidate> candidates;
    Hand hand = _hand;

    // 各牌の残り枚数を数える。
    std::vector<int> counts = count_left_tiles(hand, dora_indicators_);

    std::vector<int> tiles = UnnecessaryTileSelector::select(hand, syanten_type_);

    for (auto tile : tiles) {
        remove_tile(hand, tile);

        auto required_tiles = get_required_tiles(hand, syanten_type_, counts);

        add_tile(hand, tile);

        candidates.emplace_back(tile, required_tiles);
    }

    return candidates;
}

/**
 * @brief 各牌の残り枚数を数える。
 *
 * @param[in] hand 手牌
 * @param[in] dora_indicators ドラ表示牌の一覧
 * @return std::vector<int> 各牌の残り枚数
 */
std::vector<int> ExpectedValueCalculator::count_left_tiles(const Hand &hand,
                                                           const std::vector<int> &dora_indicators)
{
    std::vector<int> counts(37, 4); // 34 = 36 は赤牌が残っていれば1
    counts[Tile::AkaManzu5] = counts[Tile::AkaPinzu5] = counts[Tile::AkaSozu5] = 1;

    // 手牌を除く。
    for (int i = 0; i < 34; ++i)
        counts[i] -= hand.num_tiles(i);
    counts[Tile::AkaManzu5] -= hand.aka_manzu5;
    counts[Tile::AkaPinzu5] -= hand.aka_pinzu5;
    counts[Tile::AkaSozu5] -= hand.aka_sozu5;

    // 副露ブロックを除く。
    for (const auto &block : hand.melds) {
        for (auto tile : block.tiles) {
            counts[aka2normal(tile)]--;
            counts[Tile::AkaManzu5] -= tile == Tile::AkaManzu5;
            counts[Tile::AkaPinzu5] -= tile == Tile::AkaPinzu5;
            counts[Tile::AkaSozu5] -= tile == Tile::AkaSozu5;
        }
    }

    // ドラ表示牌を除く。
    for (auto tile : dora_indicators) {
        counts[aka2normal(tile)]--;
        counts[Tile::AkaManzu5] -= tile == Tile::AkaManzu5;
        counts[Tile::AkaPinzu5] -= tile == Tile::AkaPinzu5;
        counts[Tile::AkaSozu5] -= tile == Tile::AkaSozu5;
    }

    return counts;
}

/**
 * @brief 打牌一覧を取得する。
 *
 * @param[in] hand 手牌
 * @param[in] syanten 手牌の向聴数
 * @return 向聴戻し: 1、向聴数変化なし: 0、それ以外: inf
 */
#ifdef ENABLE_DISCARD_TILES_CACHE
const std::vector<int> &ExpectedValueCalculator::get_discard_tiles(Hand &hand, int syanten)
{
    auto &cache = discard_cache_[syanten];

    if (auto itr = cache.find(hand); itr != cache.end())
        return itr->second; // キャッシュが存在する場合

    std::vector<int> flags(34, std::numeric_limits<int>::max());
    for (int tile = 0; tile < 34; ++tile) {
        if (hand.contains(tile)) {
            remove_tile(hand, tile);
            auto [_, syanten_after] = SyantenCalculator::calc(hand, syanten_type_);
            add_tile(hand, tile);
            flags[tile] = syanten_after - syanten;
        }
    }

    auto [itr, _] = cache.insert_or_assign(hand, flags);

    return itr->second;
}
#else
std::vector<int> ExpectedValueCalculator::get_discard_tiles(Hand &hand, int syanten)
{
    std::vector<int> flags(34, std::numeric_limits<int>::max());
    for (int tile = 0; tile < 34; ++tile) {
        if (hand.contains(tile)) {
            remove_tile(hand, tile);
            auto [_, syanten_after] = SyantenCalculator::calc(hand, syanten_type_);
            add_tile(hand, tile);
            flags[tile] = syanten_after - syanten;
        }
    }

    return flags;
}
#endif

/**
 * @brief 自摸牌一覧を取得する。
 *
 * @param[in] hand 手牌
 * @param[in] syanten 手牌の向聴数
 * @return 自摸牌一覧
 */
#ifdef ENABLE_DRAW_TILES_CACHE
const std::vector<int> &ExpectedValueCalculator::get_draw_tiles(Hand &hand, int syanten,
                                                                const std::vector<int> &counts)
{
    auto &cache = draw_cache_[syanten];

    if (auto itr = cache.find(hand); itr != cache.end())
        return itr->second; // キャッシュが存在する場合

    std::vector<int> flags(34);

    for (int tile = 0; tile < 34; ++tile) {
        if (hand.num_tiles(tile) == 4)
            continue; // 残り牌がない場合

        add_tile(hand, tile);
        auto [_, syanten_after] = SyantenCalculator::calc(hand, syanten_type_);
        remove_tile(hand, tile);
        flags[tile] = syanten_after - syanten;
    }

    auto [itr, _] = cache.insert_or_assign(hand, flags);

    return itr->second;
}
#else
std::vector<int> ExpectedValueCalculator::get_draw_tiles(Hand &hand, int syanten,
                                                         const std::vector<int> &counts)
{
    std::vector<int> flags(34);

    for (int tile = 0; tile < 34; ++tile) {
        if (hand.num_tiles(tile) == 4)
            continue; // 残り牌がない場合

        add_tile(hand, tile);
        auto [_, syanten_after] = SyantenCalculator::calc(hand, syanten_type_);
        remove_tile(hand, tile);
        flags[tile] = syanten_after - syanten;
    }

    return flags;
}
#endif

/**
 * @brief 手牌の点数を取得する。
 *
 * @param[in] hand 手牌
 * @param[in] win_tile 自摸牌
 * @return 点数
 */
const ScoreCache &ExpectedValueCalculator::get_score(const Hand &hand, int win_tile,
                                                     const std::vector<int> &counts)
{
    ScoreKey key(hand, win_tile);
    if (auto itr = score_cache_.find(key); itr != score_cache_.end())
        return itr->second; // キャッシュが存在する場合

    // 非門前の場合は自摸のみ
    int hand_flag = hand.is_menzen() ? (HandFlag::Tumo | HandFlag::Reach) : HandFlag::Tumo;

    // 点数計算を行う。
    Result result = score_.calc(hand, win_tile, hand_flag);

    // 表ドラの数
    int n_dora = int(dora_indicators_.size());

    // ダブル立直、一発、海底撈月で最大3翻まで増加するので、
    // ベースとなる点数、+1翻の点数、+2翻の点数、+3翻の点数も計算しておく。
    std::vector<double> scores(4, 0);
    if (result.success) {
        // 役ありの場合
        std::vector<int> up_scores = score_.get_scores_for_exp(result);

        if (calc_uradora_ && n_dora == 1) {
            // 裏ドラ考慮ありかつ表ドラが1枚以上の場合は、厳密に計算する。
            std::vector<double> n_indicators(5, 0);
            int sum_indicators = 0;
            for (int tile = 0; tile < 34; ++tile) {
                int n = hand.num_tiles(tile);
                if (n > 0) {
                    // ドラ表示牌の枚数を数える。
                    n_indicators[n] += counts[Dora2Indicator.at(tile)];
                    sum_indicators += counts[Dora2Indicator.at(tile)];
                }
            }

            // 裏ドラの乗る確率を枚数ごとに計算する。
            std::vector<double> uradora_probs(5, 0);

            // 厳密に計算するなら残り枚数は数えるべきだが、あまり影響がないので121枚で固定
            int n_left_tiles = 121;
            uradora_probs[0] = double(n_left_tiles - sum_indicators) / n_left_tiles;
            for (int i = 1; i < 5; ++i)
                uradora_probs[i] = double(n_indicators[i]) / n_left_tiles;

            for (int base = 0; base < 4; ++base) {
                for (int i = 0; i < 5; ++i) { // 裏ドラ1枚の場合、最大4翻まで乗る可能性がある
                    int han_idx = std::min(base + i, int(up_scores.size() - 1));
                    scores[base] += up_scores[han_idx] * uradora_probs[i];
                }
            }
        }
        else if (calc_uradora_ && n_dora > 1) {
            // 裏ドラ考慮ありかつ表ドラが2枚以上の場合、統計データを利用する。
            for (int base = 0; base < 4; ++base) {
                for (int i = 0; i < 13; ++i) {
                    int han_idx = std::min(base + i, int(up_scores.size() - 1));
                    scores[base] += up_scores[han_idx] * uradora_prob_[n_dora][i];
                }
            }
        }
        else {
            // 裏ドラ考慮なしまたは表ドラが0枚の場合
            for (int base = 0; base < 4; ++base) {
                int han_idx = std::min(base, int(up_scores.size() - 1));
                scores[base] += up_scores[han_idx];
            }
        }
    }

    ScoreCache cache(scores);
    auto [itr, _] = score_cache_.insert_or_assign(key, cache);

    return itr->second;
}

std::vector<std::vector<double>> ExpectedValueCalculator::uradora_prob_;

} // namespace mahjong
