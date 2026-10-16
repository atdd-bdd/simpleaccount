#pragma once
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "account_type.h"
#include "text_types.h"

// The chart of accounts: one tree holding both the accounts that keep money and
// the ones Quicken calls categories. Having them in one tree is what lets every
// transaction balance with no special case for a category. See Accounts.spectable.
namespace chart {

// The identity an account is given once, when it is created, and never
// afterwards -- a plain incrementing number, unlike a transaction's
// millisecond-clock Id, because accounts are made rarely enough that nothing
// cleverer than a counter is needed. See the identity rule in
// Accounts.spectable. Zero is "none": no account is ever given that number.
class AccountId {
public:
    AccountId() = default;
    explicit AccountId(long long value) : value_(value) {}
    long long value() const { return value_; }
    bool is_none() const { return value_ == 0; }
    bool operator==(const AccountId& o) const { return value_ == o.value_; }
    bool operator!=(const AccountId& o) const { return value_ != o.value_; }

private:
    long long value_ = 0;
};

// The identity a category is given once, when it is created, and never
// afterwards -- drawn from the same counter as AccountId, so the two never
// collide. See the identity rule in Accounts.spectable. Zero is "none".
class CategoryId {
public:
    CategoryId() = default;
    explicit CategoryId(long long value) : value_(value) {}
    long long value() const { return value_; }
    bool is_none() const { return value_ == 0; }
    bool operator==(const CategoryId& o) const { return value_ == o.value_; }
    bool operator!=(const CategoryId& o) const { return value_ != o.value_; }

private:
    long long value_ = 0;
};

// A node Quicken calls a category -- Income, Expense or Equity -- kept apart
// from Account because almost nothing else about it is shared. See the
// Category entity in Accounts.spectable.
struct Category {
    CategoryId id;
    CategoryId subcategory_of;   // 0 is top-level; otherwise the parent category
    types::AccountPath path;
    types::AccountType type = types::AccountType::Expense;
    bool placeholder = false;
    bool hidden = false;
    // A short way to type it; see the finding rule in Accounts.spectable.
    std::string alias;
};

// A row as it would be read from storage before its path is derived: named by
// id and parent id, the way a future schema holds it, rather than by the path
// a person types. See the path-derivation scenario in Accounts.spectable.
struct CategoryRawRow {
    long long id = 0;
    long long subcategory_of = 0;
    std::string name;
    types::AccountType type = types::AccountType::Expense;
};

// Walks each row's SubcategoryOf chain back to a top-level category (0),
// joining the names from the root down, the same way an account's path is
// worked out from the tree for display rather than stored as text. One
// output row per input row, in the input's own order.
inline std::vector<Category> categories_from_raw(const std::vector<CategoryRawRow>& rows) {
    std::map<long long, const CategoryRawRow*> by_id;
    for (const CategoryRawRow& r : rows) by_id[r.id] = &r;

    std::vector<Category> out;
    for (const CategoryRawRow& r : rows) {
        std::vector<std::string> segments;
        const CategoryRawRow* cur = &r;
        while (cur != nullptr) {
            segments.push_back(cur->name);
            if (cur->subcategory_of == 0) break;
            const auto at = by_id.find(cur->subcategory_of);
            cur = at == by_id.end() ? nullptr : at->second;
        }
        std::string path;
        for (auto it = segments.rbegin(); it != segments.rend(); ++it)
            path += (path.empty() ? "" : ":") + *it;

        Category c;
        c.id = CategoryId(r.id);
        c.subcategory_of = CategoryId(r.subcategory_of);
        c.path = types::AccountPath(path);
        c.type = r.type;
        out.push_back(c);
    }
    return out;
}

// A reference to an account or a category, from the one shared counter the
// two draw their ids from -- see the id-collision rule in Accounts.spectable.
// This is what a posting or a transaction holds now instead of a path: it
// cannot go stale when the node it names is renamed or moved, because
// nothing about it is text. Zero is "none"; the reserved value -1 is the
// sentinel for a split, whose parts live in TransactionSplit rather than at
// any id. See the section on naming both accounts directly in
// Transactions.spectable.
class Ref {
public:
    Ref() = default;
    explicit Ref(long long value) : value_(value) {}
    long long value() const { return value_; }
    bool is_none() const { return value_ == 0; }
    bool operator==(const Ref& o) const { return value_ == o.value_; }
    bool operator!=(const Ref& o) const { return value_ != o.value_; }
    bool operator<(const Ref& o) const { return value_ < o.value_; }

private:
    long long value_ = 0;
};

inline constexpr long long kSplitRefValue = -1;
inline Ref split_ref() { return Ref(kSplitRefValue); }
inline bool is_split(const Ref& r) { return r.value() == kSplitRefValue; }

struct Account {
    AccountId id;
    types::AccountPath path;
    types::AccountType type = types::AccountType::Checking;
    bool placeholder = false;   // a grouping node; takes no postings
    bool hidden = false;        // kept out of pickers, history retained
    // A short way to type this account when categorising; see the finding rule
    // in Accounts.spectable. Empty for most accounts.
    std::string alias;
    // What a payment to this account is called on the statement of whatever
    // account pays it -- CHASEBANK on the bank statement, for a Chase card.
    // It is how a payment on one side finds the account on the other without
    // searching every account in the book. Several accounts may share one.
    std::string payment_payee;
    // What is kept about the account itself, from its own dialog: which
    // institution it is at, and the number on the statement. The number is
    // kept exactly as written -- dashes, spaces, a leading zero -- because it
    // is a label and not an amount. See the account-settings section of
    // Accounts.spectable.
    std::string bank;
    std::string account_number;
};

// Why an operation was refused. The text is what the register shows, so it names
// the account and says what to do instead rather than only what went wrong.
struct Rejection {
    bool refused = false;
    std::string reason;
};

class Chart {
public:
    // How many postings an account has. The chart does not hold postings, so a
    // caller that wants the rules about deleting and retyping has to say.
    using PostingCounts = std::map<std::string, int>;

    void set_posting_counts(const PostingCounts& counts) { counts_ = counts; }

    int postings_on(const std::string& path) const {
        const auto at = counts_.find(path);
        return at == counts_.end() ? 0 : at->second;
    }

    bool has(const std::string& path) const { return accounts_.count(path) != 0; }

    const Account* find(const std::string& path) const {
        const auto at = accounts_.find(path);
        return at == accounts_.end() ? nullptr : &at->second;
    }

    // Resolving between a path and the id a posting or a transaction now
    // holds instead. "Split" is the one path that is never a real node;
    // anything else is looked up among the accounts and then the
    // categories, since the two never collide. Empty and not found both
    // come back as Ref(), which a caller checks for the same way either way.
    Ref ref_for_path(const std::string& path) const {
        if (path.empty()) return Ref();
        if (path == "Split") return split_ref();
        const Account* a = find(path);
        if (a != nullptr) return Ref(a->id.value());
        const Category* c = find_category(path);
        if (c != nullptr) return Ref(c->id.value());
        return Ref();
    }

    // The other direction: what a ref is shown as while every table and
    // every display still reads in text. A few hundred nodes at most, so a
    // linear search over each collection costs nothing worth avoiding.
    std::string path_for_ref(const Ref& r) const {
        if (r.is_none()) return "";
        if (is_split(r)) return "Split";
        if (const Account* a = find_account_by_ref(r)) return a->path.value();
        if (const Category* c = find_category_by_ref(r)) return c->path.value();
        return "";
    }

    const Account* find_account_by_ref(const Ref& r) const {
        if (r.is_none() || is_split(r)) return nullptr;
        for (const auto& entry : accounts_)
            if (entry.second.id.value() == r.value()) return &entry.second;
        return nullptr;
    }

    const Category* find_category_by_ref(const Ref& r) const {
        if (r.is_none() || is_split(r)) return nullptr;
        for (const auto& entry : categories_)
            if (entry.second.id.value() == r.value()) return &entry.second;
        return nullptr;
    }

    // Whether the node a ref names is real or nominal, which is all most
    // callers that are not displaying text need to know. A ref that resolves
    // to nothing is treated as nominal: the safer wrong answer, since it
    // keeps unknown money out of net worth rather than in it.
    bool is_real(const Ref& r) const {
        if (const Account* a = find_account_by_ref(r)) return types::class_of(a->type) == types::AccountClass::Real;
        return false;
    }

    // Every account, in path order, which is also the order a report wants.
    std::vector<Account> all() const {
        std::vector<Account> out;
        for (const auto& entry : accounts_) out.push_back(entry.second);
        return out;
    }

    // Used to establish a chart in a Given, where nothing is being tested about
    // how it was built, and to bring one in from storage. Either way the
    // counter catches up to the highest id it has seen, so an id handed in
    // from outside -- a book read back from disk -- is never handed out again
    // to the next account created by hand. A Given table names accounts by
    // path only and never fills in Id, so an account arriving with none gets
    // one here -- otherwise every such account would share the id "none" and
    // every Ref naming one of them would be indistinguishable from empty.
    void put(Account a) {
        if (a.id.is_none()) a.id = AccountId(++next_id_);
        if (a.id.value() > next_id_) next_id_ = a.id.value();
        accounts_[a.path.value()] = a;
    }

    // Categories are a separate collection from Accounts, but one counter: see
    // the id-collision rule in Accounts.spectable. Used to establish a chart
    // in a Given, and to bring one in from storage, the same as put() above.
    void put_category(Category c) {
        if (c.id.is_none()) c.id = CategoryId(++next_id_);
        if (c.id.value() > next_id_) next_id_ = c.id.value();
        categories_[c.path.value()] = c;
    }

    bool has_category(const std::string& path) const { return categories_.count(path) != 0; }

    const Category* find_category(const std::string& path) const {
        const auto at = categories_.find(path);
        return at == categories_.end() ? nullptr : &at->second;
    }

    std::vector<Category> all_categories() const {
        std::vector<Category> out;
        for (const auto& entry : categories_) out.push_back(entry.second);
        return out;
    }

    // Adding a category draws from the same counter as adding an account, so
    // the two never collide; see the id-collision rule in Accounts.spectable.
    // Mirrors add() above, including filling in a missing parent as a
    // placeholder.
    Rejection add_category(const types::AccountPath& path, types::AccountType type,
                           std::vector<Category>* created = nullptr) {
        if (has_category(path.value()))
            return {true, "A category named " + path.value() + " exists"};

        const std::vector<std::string> segments = types::segments_of(path);
        std::string so_far;
        for (std::size_t i = 0; i < segments.size(); ++i) {
            so_far += (i == 0 ? "" : ":") + segments[i];
            if (has_category(so_far)) continue;
            Category c;
            c.id = CategoryId(++next_id_);
            c.path = types::AccountPath(so_far);
            c.type = type;
            c.placeholder = (i + 1 < segments.size());
            categories_[so_far] = c;
            if (created) created->push_back(c);
        }
        return {};
    }

    // Adding an account creates any missing level above it as a placeholder,
    // because importing twenty years of data names deep categories that were
    // never created by hand. A filled-in parent takes the type of its child, so
    // the roll-up in a report adds up.
    Rejection add(const types::AccountPath& path, types::AccountType type,
                  std::vector<Account>* created = nullptr) {
        if (has(path.value()))
            return {true, "An account named " + path.value() + " exists"};

        const std::vector<std::string> segments = types::segments_of(path);
        std::string so_far;
        for (std::size_t i = 0; i < segments.size(); ++i) {
            so_far += (i == 0 ? "" : ":") + segments[i];
            if (has(so_far)) continue;
            Account a;
            a.id = AccountId(++next_id_);
            a.path = types::AccountPath(so_far);
            a.type = type;
            a.placeholder = (i + 1 < segments.size());
            accounts_[so_far] = a;
            if (created) created->push_back(a);
        }
        return {};
    }

    // The path of a child is built from the path of its parent, so a rename
    // rewrites every path beneath it. Postings are untouched: they refer to the
    // account, not to the text of its path.
    Rejection rename(const types::AccountPath& path, const types::AccountName& new_name) {
        if (!has(path.value()))
            return {true, "There is no account named " + path.value()};
        const std::string parent = types::parent_of(path);
        const std::string to = parent.empty() ? new_name.value()
                                              : parent + ":" + new_name.value();
        return move_subtree(path.value(), to);
    }

    Rejection move(const types::AccountPath& path, const types::AccountPath& new_parent) {
        if (!has(path.value()))
            return {true, "There is no account named " + path.value()};
        // Moving an account beneath itself would make it its own ancestor.
        if (is_descendant_or_self(new_parent.value(), path.value()))
            return {true, "An account may not be its own descendant"};
        const std::string to = new_parent.value() + ":" + types::name_of(path);
        return move_subtree(path.value(), to);
    }

    // Twenty years of history is the point of the program, so nothing silently
    // discards a posting. Hide the account instead.
    Rejection remove(const types::AccountPath& path) {
        const int n = postings_on(path.value());
        if (n > 0)
            return {true, path.value() + " has " + std::to_string(n) +
                          " postings; hide it instead"};
        accounts_.erase(path.value());
        return {};
    }

    Rejection hide(const types::AccountPath& path) {
        const auto at = accounts_.find(path.value());
        if (at == accounts_.end())
            return {true, "There is no account named " + path.value()};
        at->second.hidden = true;
        return {};
    }

    // Changing an Expense into a Bank would change the sign that every existing
    // posting is read with, and so rewrite history in silence. Within one class
    // the display sign is the same, so no posting changes meaning.
    Rejection change_type(const types::AccountPath& path, types::AccountType to) {
        const auto at = accounts_.find(path.value());
        if (at == accounts_.end())
            return {true, "There is no account named " + path.value()};
        const bool crosses = types::class_of(at->second.type) != types::class_of(to);
        if (crosses && postings_on(path.value()) > 0) {
            const bool was_nominal =
                types::class_of(at->second.type) == types::AccountClass::Nominal;
            return {true, path.value() + " has postings; " +
                          (was_nominal ? "a category cannot become an account"
                                       : "an account cannot become a category")};
        }
        at->second.type = to;
        return {};
    }

    // What a category picker offers: the accounts that can actually take a
    // posting. A placeholder cannot, and a hidden one is deliberately out of the
    // way while its history still reports.
    std::vector<Account> picker() const {
        std::vector<Account> out;
        for (const auto& entry : accounts_)
            if (!entry.second.placeholder && !entry.second.hidden)
                out.push_back(entry.second);
        return out;
    }

    std::vector<Account> children_of(const std::string& parent) const {
        std::vector<Account> out;
        for (const auto& entry : accounts_)
            if (types::parent_of(entry.second.path) == parent) out.push_back(entry.second);
        return out;
    }

    static bool is_descendant_or_self(const std::string& candidate,
                                      const std::string& ancestor) {
        if (candidate == ancestor) return true;
        return candidate.size() > ancestor.size() &&
               candidate.compare(0, ancestor.size(), ancestor) == 0 &&
               candidate[ancestor.size()] == ':';
    }

    // The one line of text an account or a category is shown as, worked out
    // from the tree rather than stored: a category reads as its name and
    // everything between it and its section, joined with colons, with the
    // section itself following in parentheses; a real account reads as its
    // own name in square brackets, with nothing of its ancestors in it. See
    // the display-conversion rule in Accounts.spectable.
    std::string display_of(const std::string& path) const {
        const auto at = accounts_.find(path);
        types::AccountType type = types::AccountType::Checking;
        if (at != accounts_.end()) {
            type = at->second.type;
        } else {
            const auto cat = categories_.find(path);
            if (cat != categories_.end()) type = cat->second.type;
        }
        if (types::class_of(type) == types::AccountClass::Nominal) {
            const std::size_t colon = path.find(':');
            const std::string without_section =
                colon == std::string::npos ? path : path.substr(colon + 1);
            return without_section + " (" + types::to_string(type) + ")";
        }
        return "[" + types::name_of(types::AccountPath(path)) + "]";
    }

    // Converts every account at once -- a few hundred at most, so repeating
    // the whole of it on every add, rename, move or hide is simpler than
    // patching only the one that changed, and still instant. See the
    // rebuilding scenario in Accounts.spectable.
    void rebuild_display_names() {
        display_.clear();
        for (const auto& entry : accounts_) display_[entry.first] = display_of(entry.first);
        for (const auto& entry : categories_) display_[entry.first] = display_of(entry.first);
    }

    // The cached conversion, or a freshly worked-out one if the cache has not
    // been built yet -- so a caller is never wrong, only possibly a rebuild
    // behind.
    std::string displayed(const std::string& path) const {
        const auto at = display_.find(path);
        return at == display_.end() ? display_of(path) : at->second;
    }

    // The number the counter last handed out. Recorded with the book, so no
    // id is ever given out twice: see the counter rule in Accounts.spectable
    // and the storage rule in Books.
    long long last_id_used() const { return next_id_; }

    // What the book recorded, read back when it is opened. It never lowers the
    // counter -- the chart is loaded first, and a row carrying a higher id than
    // the book recorded would otherwise have its number handed out again.
    void set_last_id_used(long long value) {
        if (value > next_id_) next_id_ = value;
    }

private:
    std::map<std::string, Account> accounts_;
    std::map<std::string, Category> categories_;
    std::map<std::string, std::string> display_;
    PostingCounts counts_;
    long long next_id_ = 0;

    Rejection move_subtree(const std::string& from, const std::string& to) {
        std::vector<Account> moved;
        for (auto it = accounts_.begin(); it != accounts_.end();) {
            if (is_descendant_or_self(it->first, from)) {
                Account a = it->second;
                a.path = types::AccountPath(to + it->first.substr(from.size()));
                moved.push_back(a);
                it = accounts_.erase(it);
            } else {
                ++it;
            }
        }
        for (const Account& a : moved) accounts_[a.path.value()] = a;
        return {};
    }
};

// A ref-to-node lookup, built once for a computation that reads many
// postings. Chart::path_for_ref walks every account and then every category,
// which costs nothing for one posting and is quadratic for a book's worth of
// them: twenty years of history is tens of thousands of postings against five
// hundred accounts, and the walk was being done for every one of them.
//
// It is built from the chart as it stands and is good for the length of one
// computation. It does not notice a chart that changes underneath it, which is
// why it is not kept on the Chart itself: an index that can go stale is how a
// transaction ends up reported against the wrong account, and no amount of
// speed is worth that.
class RefIndex {
public:
    explicit RefIndex(const Chart& accounts) {
        for (const Account& a : accounts.all())
            nodes_[a.id.value()] =
                Node{a.path.value(), types::class_of(a.type) == types::AccountClass::Real};
        for (const Category& c : accounts.all_categories())
            nodes_[c.id.value()] = Node{c.path.value(), false};
    }

    // The same answers Chart::path_for_ref and Chart::is_real give, including
    // for the two refs that name no node: none is empty and a split is
    // "Split", neither of which is looked up.
    // By reference, not by value: a register asks this of every transaction
    // in the book, and a find asks it of every transaction once per register.
    // Returning a copy was eight million string allocations on the author's
    // book.
    const std::string& path_for(const Ref& r) const {
        static const std::string nothing;
        static const std::string split_path("Split");
        if (r.is_none()) return nothing;
        if (is_split(r)) return split_path;
        const auto at = nodes_.find(r.value());
        return at == nodes_.end() ? nothing : at->second.path;
    }

    bool is_real(const Ref& r) const {
        if (r.is_none() || is_split(r)) return false;
        const auto at = nodes_.find(r.value());
        return at != nodes_.end() && at->second.real;
    }

private:
    struct Node {
        std::string path;
        bool real = false;
    };
    std::unordered_map<long long, Node> nodes_;
};

}  // namespace chart
