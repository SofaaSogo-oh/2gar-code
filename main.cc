#include <algorithm>
#include <bit>
#include <boost/program_options.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <mutex>
#include <numeric>
#include <omp.h>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using mask_t = uint64_t;

template <typename Iterator> class ConvexSetPrinter {
public:
  ConvexSetPrinter(Iterator begin, Iterator end) : begin_(begin), end_(end) {}

  friend std::ostream &operator<<(std::ostream &os,
                                  const ConvexSetPrinter &printer) {
    os << "{";
    for (auto it = printer.begin_; it != printer.end_; ++it) {
      if (it != printer.begin_)
        os << ", ";
      os << "{";
      bool first = true;
      for (int bit = 0; bit < 64; ++bit) {
        if (*it & (1ULL << bit)) {
          if (!first)
            os << ", ";
          os << bit;
          first = false;
        }
      }
      os << "}";
    }
    os << "}";
    return os;
  }

private:
  Iterator begin_;
  Iterator end_;
};

template <typename Iterator>
ConvexSetPrinter<Iterator> print_convex_sets(Iterator begin, Iterator end) {
  return ConvexSetPrinter<Iterator>(begin, end);
}

struct Config {
  uint64_t dim = 0;
  uint64_t start_idx = 0;
  uint64_t success_count = 0;
  std::string convex_output_mode = "none";
  uint64_t block_size = 100'000'000;
  std::string timermode = "seconds";
};

Config parse_args(int argc, char *argv[]) {
  namespace po = boost::program_options;
  Config cfg;

  po::options_description desc("Allowed options");
  desc.add_options()("help", "produce help message")(
      "dim", po::value<uint64_t>(&cfg.dim)->required(),
      "dimension of the space")(
      "start", po::value<uint64_t>(&cfg.start_idx)->default_value(0),
      "starting index for convex sets")(
      "success", po::value<uint64_t>(&cfg.success_count)->default_value(0),
      "initial count of successful convex sets")(
      "output",
      po::value<std::string>(&cfg.convex_output_mode)->default_value("none"),
      "output mode for convex sets (none, index, set, table, delim)")(
      "block", po::value<uint64_t>(&cfg.block_size)->default_value(100'000'000),
      "size of the block for processing convex sets")(
      "timermode",
      po::value<std::string>(&cfg.timermode)->default_value("seconds"));

  po::variables_map vm;
  try {
    po::store(po::parse_command_line(argc, argv, desc), vm);
    if (vm.count("help")) {
      std::cout << desc << "\n";
      std::exit(0);
    }
    po::notify(vm);
  } catch (const po::error &e) {
    std::cerr << "Error: " << e.what() << "\n";
    std::cerr << desc << "\n";
    std::exit(1);
  }

  return cfg;
}

[[nodiscard]] constexpr inline bool contains(mask_t a, mask_t b) {
  return (a & b) == b;
}
std::mutex cout_mutex;

std::vector<mask_t> get_all_masks(int dim) {
  std::vector<mask_t> res(1ull << dim);
  std::iota(res.begin(), res.end(), 0);
  return res;
}

std::vector<mask_t> find_check_masks(const std::vector<mask_t> &masks) {
  std::vector<mask_t> res;
  std::copy_if(masks.begin(), masks.end(), std::back_inserter(res),
               [](auto &&m) { return std::popcount(m) <= 2; });
  return res;
}

mask_t find_hull(mask_t x, const std::vector<mask_t> &convex) {
  mask_t hull = ~mask_t(0);
  for (mask_t y : convex) {
    if (contains(y, x)) {
      hull &= y;
    }
  }
  return hull;
}

bool check_convex(const std::vector<mask_t> &convex,
                  const std::vector<mask_t> &all_masks,
                  const std::vector<mask_t> &check_masks, int dim) {
  std::vector<mask_t> hull_for(check_masks.size());
  std::transform(check_masks.begin(), check_masks.end(), hull_for.begin(),
                 [&convex](mask_t m) { return find_hull(m, convex); });

  std::vector<bool> in_convex(1u << dim, false);
  for (mask_t m : convex)
    in_convex[m] = true;

  for (mask_t m : all_masks) {
    bool exist_holds = true;
    for (size_t i = 0; i < check_masks.size(); ++i) {
      mask_t key = check_masks[i];
      if (contains(m, key)) {
        if (!contains(m, hull_for[i])) {
          exist_holds = false;
          break;
        }
      }
    }
    if (in_convex[m] != exist_holds)
      return false;
  }
  return true;
}

inline mask_t convex_to_idx(const std::vector<mask_t> &convex) {
  mask_t idx = 0;
  std::for_each(std::next(convex.begin()), convex.end(),
                [&idx](mask_t m) { idx |= 1ull << m; });
  return idx;
}

int main(int argc, char *argv[]) {
  std::cout << "Max number of threads: " << omp_get_max_threads() << "\n";
  auto &&cfg = parse_args(argc, argv);
  uint64_t dim = cfg.dim;
  std::cout << "dim: " << dim << "\n";
  std::cout << "start_idx: " << cfg.start_idx
            << ", initial success_count: " << cfg.success_count << "\n";
  std::cout << "block_size: " << cfg.block_size << "\n";
  std::cout << "convex_output_mode: " << cfg.convex_output_mode << "\n";

  std::vector<mask_t> all_masks = get_all_masks(dim);
  std::vector<mask_t> check_masks = find_check_masks(all_masks);
  mask_t max_mask = (1ull << dim) - 1;

  size_t n_others = all_masks.size() - 1;
  uint64_t total_convex = 1ull << n_others;
  uint64_t total_work = total_convex;

  std::ofstream ofs{"dest.txt"};

  using record_method_t_real = std::function<void(std::ostream &, uint64_t,
                                                  const std::vector<mask_t> &)>;
  // record_methods :: Map String (ofs -> idx -> convex -> IO ())
  auto record_methods = std::unordered_map<std::string, record_method_t_real>{
      {"index", [](std::ostream &os, uint64_t idx,
                   const std::vector<mask_t> &convex) { os << idx << "\n"; }},
      {"set",
       [](std::ostream &os, uint64_t idx, const std::vector<mask_t> &convex) {
         os << print_convex_sets(convex.begin(), convex.end()) << "\n";
       }},
      {"table",
       [](std::ostream &os, uint64_t idx, const std::vector<mask_t> &convex) {
         os << std::left << std::setw(20) << idx
            << print_convex_sets(convex.begin(), convex.end()) << "\n";
       }},
      {"delim", [](std::ostream &os, uint64_t idx,
                   const std::vector<mask_t> &convex) { os << idx << ", "; }},
  };
  using record_method_t =
      std::move_only_function<void(uint64_t, const std::vector<mask_t> &)>;

  auto record_convex = record_method_t{[](auto, auto) {}};
  if (record_methods.contains(cfg.convex_output_mode)) {
    try {
      auto convex_ofs = std::ofstream{"convex_sets.txt"};
      auto tmp = record_methods.at(cfg.convex_output_mode);
      record_convex = [tmp,
                       convex_ofs = std::move(convex_ofs)]<typename... Args>(
                          Args &&...args) mutable {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::invoke(tmp, convex_ofs, std::forward<Args>(args)...);
      };
    } catch (...) {
      std::cerr << "Failed to open convex_sets.txt for writing. "
                << "Convex sets will not be recorded.\n";
    }
  } else {
    std::cout << "Convex output mode set to 'none' or unrecognized. "
                 "Convex sets will not be recorded.\n";
  }

  auto &&begin = std::chrono::steady_clock::now();
  auto success_count = cfg.success_count;
  using timer_method_t = std::function<std::chrono::duration<long double>(
      std::chrono::steady_clock::time_point)>;
  auto timer_methods = std::unordered_map<std::string, timer_method_t>{
      {"seconds",
       [begin](auto end) -> std::chrono::duration<long double> {
         return std::chrono::duration_cast<std::chrono::seconds>(end - begin);
       }},
      {"milliseconds",
       [begin](auto end) -> std::chrono::duration<long double> {
         return std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                      begin);
       }},
      {"microseconds",
       [begin](auto end) -> std::chrono::duration<long double> {
         return std::chrono::duration_cast<std::chrono::microseconds>(end -
                                                                      begin);
       }},
      {"nanoseconds", [begin](auto end) -> std::chrono::duration<long double> {
         return std::chrono::duration_cast<std::chrono::nanoseconds>(end -
                                                                     begin);
       }}};
  timer_method_t timer_measure = timer_methods["seconds"];
  if (timer_methods.contains(cfg.timermode))
    timer_measure = timer_methods.at(cfg.timermode);

  for (uint64_t progress = cfg.start_idx; progress < total_convex;
       progress += cfg.block_size) {
    auto block_start_idx = progress;
    auto block_end_idx = std::min(progress + cfg.block_size, total_convex);
#pragma omp parallel
    {
#pragma omp for reduction(+ : success_count)
      for (uint64_t idx = block_start_idx; idx < block_end_idx; ++idx) {
        std::vector<mask_t> convex;
        convex.reserve(std::popcount(idx) + 1);
        convex.push_back(max_mask);

        for (size_t j = 0; j < n_others; ++j) {
          if (idx & (1ULL << j)) {
            convex.push_back(static_cast<mask_t>(j));
          }
        }

        if (check_convex(convex, all_masks, check_masks, dim)) {
          ++success_count;
          record_convex(idx, convex);
        }
      }
    }
    std::cout << "\rProgress: " << block_end_idx << "/" << total_work << " ("
              << 100.0 * block_end_idx / total_work << "%)"
              << ", success count: " << success_count << std::flush;
    auto &&end = std::chrono::steady_clock::now();
    ofs << block_end_idx << " " << success_count << " "
        << timer_measure(end).count() << std::endl;
  }

  auto &&end = std::chrono::steady_clock::now();
  std::cout << std::endl
            << "Number of convex sets satisfying the condition: "
            << success_count << std::endl;

  auto elapsed_ms = timer_measure(end);
  std::cout << "The time: " << elapsed_ms.count() << " s\n";
  return 0;
}