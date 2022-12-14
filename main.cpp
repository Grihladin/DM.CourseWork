#include <fstream>
#include <iostream>
#include <memory>

#define DEBUG

namespace std {
template <class T, class... Args>
static constexpr T* construct_at(T* p, Args&&... args)
{
    return reinterpret_cast<T*>(new (static_cast<void*>(p)) T(forward<Args>(args)...));
}
};

namespace custom {

template <class _Alloc, class S>
struct __rebind_alloc_helper;

template <template <class, class...> class _Alloc, class New, class Old, class... OtherArgs>
struct __rebind_alloc_helper<_Alloc<Old, OtherArgs...>, New> {
    typedef typename std::allocator_traits<_Alloc<New, OtherArgs...>>::allocator_type type;
};

template <class, class, class>
class set;

namespace details {

    template <typename T>
    class SetNode {
        template <class, class, class>
        friend class ::custom::set;

    public:
        enum class Color {
            Red = 0,
            Black,
        };

        using node_type = SetNode;
        using node_pointer = node_type*;
        using value_type = T;
        using color_type = Color;
        using const_reference = const value_type&;

        explicit SetNode(const value_type& value)
            : m_value(value)
        {
        }
        explicit SetNode(value_type&& value)
            : m_value(std::move(value))
        {
        }

        ~SetNode() = default;

        void set_parent(node_pointer pc) { m_parent = pc; }
        void set_left_child(node_pointer lc) { m_left_child = lc; }
        void set_right_child(node_pointer rc) { m_right_child = rc; }
        void set_color(color_type cl) { m_color = cl; }
        void set_value(const_reference val) { m_value = val; }
        void set_value(value_type&& val) { m_value = std::move(val); }

        node_pointer left_child() const { return m_left_child; }
        node_pointer right_child() const { return m_right_child; }
        node_pointer parent() const { return m_parent; }
        color_type color() const { return m_color; }

        bool is_red() const { return color() == color_type::Red; }
        bool is_black() const { return color() == color_type::Black; }
        bool is_nil_node() const { return m_is_nil_node; }
        const_reference value() const { return m_value; }
        value_type&& value() { return std::move(m_value); }

    protected:
        SetNode(bool is_nil_node, color_type color)
            : m_is_nil_node(is_nil_node)
            , m_color(color)
        {
        }

    private:
        color_type m_color { Color::Red };
        node_pointer m_parent { nullptr };
        node_pointer m_left_child { nullptr };
        node_pointer m_right_child { nullptr };
        value_type m_value;
        bool m_is_nil_node { false };
    };
}

template <class Key, class Compare = std::less<Key>, class Allocator = std::allocator<Key>>
class set {
public:
    using key_type = Key;
    using value_type = Key;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using key_compare = Compare;
    using value_compare = Compare;
    using allocator_type = Allocator;
    using refernce = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using node_type = details::SetNode<value_type>;

private:
    using node_pointer = node_type*;
    using node_const_pointer = const node_type*;
    typedef typename __rebind_alloc_helper<allocator_type, node_type>::type node_alloc_type;

public:
    set()
        : m_nil_node(true, node_type::Color::Black)
    {
        m_root = nil_node();
    }

    set(const Compare& comp, const Allocator& alloc = Allocator())
        : m_comp(comp)
        , m_allocator(alloc)
        , m_nil_node(true, node_type::Color::Black)
    {
        m_root = nil_node();
    }

    explicit set(const Allocator& alloc)
        : m_allocator(alloc)
        , m_nil_node(true, node_type::Color::Black)
    {
        m_root = nil_node();
    }

    // TODO: Return std::pair<iterator, bool>
    inline bool insert(const value_type& value) { return insert(value_type(value)); }
    inline bool insert(value_type&& value)
    {
        node_pointer new_node = _btree_insert(std::move(value));
        if (!new_node) {
            return false;
        }

        balance_after_insert(new_node);
        return true;
    }

    size_type erase(const_reference value)
    {
        node_pointer node_to_delete = _btree_find(value);
        if (!node_to_delete || node_to_delete->is_nil_node()) {
            return 0;
        }

        auto children_count = [](node_pointer node) -> size_t {
            return (size_t)(!node->left_child()->is_nil_node()) + (size_t)(!node->right_child()->is_nil_node());
        };

        auto destroy_node = [this](node_pointer node) {
            if (node && node->is_nil_node()) {
                return;
            }
            std::destroy_at(node);
            this->m_node_allocator.deallocate(node, 1);
        };

        if (children_count(node_to_delete) == 0) {
            if (is_root(node_to_delete)) {
                m_root = nil_node();
            } else {
                if (is_left_child(node_to_delete)) {
                    node_to_delete->parent()->set_left_child(nil_node());
                } else {
                    node_to_delete->parent()->set_right_child(nil_node());
                }
            }
            return 1;
        }

        node_pointer left_most = _btree_left_most(node_to_delete->right_child());
        node_pointer child = left_most->right_child();
        if (left_most == nil_node()) {
            child = nil_node();
            left_most = node_to_delete->left_child();
        }

        child->set_parent(left_most->parent());
        if (is_root(left_most)) {
            m_root = child;
        } else {
            if (is_left_child(left_most)) {
                left_most->parent()->set_left_child(child);
            } else {
                left_most->parent()->set_right_child(child);
            }
        }

        if (node_to_delete != left_most) {
            node_to_delete->set_value(std::move(left_most->value()));
        }

        if (left_most->is_black()) {
            balance_after_delete(child);
        }

        destroy_node(left_most);
        return 1;
    }

    allocator_type get_allocator() const noexcept { return m_allocator; }
    size_type size() const { return m_size; }
    bool empty() const { return size() == 0; }

#ifdef DEBUG
    // TODO: Debug will be removed
    int print_id;
    void print_loop(node_pointer node, std::ofstream& file)
    {
        if (!node || node->is_nil_node()) {
            return;
        }
        int my_id = print_id++;
        static const char* k[] = { "r", "b" };
        file << std::to_string(my_id) << " [label=\"" << std::to_string(node->value()) + k[node->is_black()] << "\"];\n";
        if (node->left_child() && !node->left_child()->is_nil_node())
            file << std::to_string(my_id) << " -> " << std::to_string(print_id) << "\n";
        print_loop(node->left_child(), file);
        if (node->right_child() && !node->right_child()->is_nil_node())
            file << std::to_string(my_id) << " -> " << std::to_string(print_id) << "\n";
        print_loop(node->right_child(), file);
    }

    void print(std::ofstream& file)
    {
        std::cout << "Printing" << std::endl;
        print_id = 0;
        file << "digraph AST {\n";
        file << "node [shape=box];\n";
        print_loop(m_root, file);
        file << "}\n";
    }
#endif

private:
    bool is_root(node_pointer node) const { return node == m_root; }

    inline bool is_left_child(node_pointer of_node) const
    {
        if (!of_node->parent()) {
            return false;
        }
        return of_node->parent()->left_child() == of_node;
    }

    inline bool is_right_child(node_pointer of_node) const
    {
        if (!of_node->parent()) {
            return false;
        }
        return of_node->parent()->right_child() == of_node;
    }

    // Warning: it does NOT check correctness!
    inline bool has_uncle(node_pointer of_node) const { return !!uncle(of_node); }
    inline node_pointer uncle(node_pointer of_node) const
    {
        if (of_node->parent()->parent()->left_child() == of_node->parent()) {
            return of_node->parent()->parent()->right_child();
        } else {
            return of_node->parent()->parent()->left_child();
        }
    }

    void balance_after_insert(node_pointer new_node)
    {
        while (!is_root(new_node) && new_node->parent()->is_red()) {
            if (has_uncle(new_node) && uncle(new_node)->is_red()) {
                new_node->parent()->set_color(node_type::Color::Black);
                new_node->parent()->parent()->set_color(node_type::Color::Red);
                uncle(new_node)->set_color(node_type::Color::Black);
                new_node = new_node->parent()->parent();
            } else {
                if (is_left_child(new_node->parent())) {
                    if (is_right_child(new_node)) {
                        new_node = new_node->parent();
                        rotate<RotateType::Left>(new_node);
                    }
                    new_node->parent()->set_color(node_type::Color::Black);
                    new_node->parent()->parent()->set_color(node_type::Color::Red);
                    rotate<RotateType::Right>(new_node->parent()->parent());
                } else {
                    if (is_left_child(new_node)) {
                        new_node = new_node->parent();
                        rotate<RotateType::Right>(new_node);
                    }
                    new_node->parent()->set_color(node_type::Color::Black);
                    new_node->parent()->parent()->set_color(node_type::Color::Red);
                    rotate<RotateType::Left>(new_node->parent()->parent());
                }
            }
        }

        m_root->set_color(node_type::Color::Black);
    }

    void balance_after_delete(node_pointer node)
    {
        while (!is_root(node) && node->is_black()) {
            if (is_left_child(node)) {
                node_pointer brother = node->parent()->right_child();
                if (brother && brother->is_red()) {
                    brother->set_color(node_type::Color::Black);
                    node->parent()->set_color(node_type::Color::Red);
                    rotate<RotateType::Left>(node->parent());
                    brother = node->parent()->right_child();
                }
                if (brother->left_child()->is_black() && brother->right_child()->is_black()) {
                    brother->set_color(node_type::Color::Red);
                    node = node->parent();
                } else {
                    if (brother->right_child()->is_black()) {
                        brother->left_child()->set_color(node_type::Color::Black);
                        brother->set_color(node_type::Color::Red);
                        rotate<RotateType::Right>(brother);
                        brother = node->parent()->right_child();
                    }
                    brother->set_color(node->parent()->color());
                    node->parent()->set_color(node_type::Color::Black);
                    brother->right_child()->set_color(node_type::Color::Black);
                    rotate<RotateType::Left>(node->parent());
                    node = m_root;
                }
            } else {
                node_pointer brother = node->parent()->left_child();
                if (brother && brother->is_red()) {
                    brother->set_color(node_type::Color::Black);
                    node->parent()->set_color(node_type::Color::Red);
                    rotate<RotateType::Right>(node->parent());
                    brother = node->parent()->left_child();
                }
                if (brother->left_child()->is_black() && brother->right_child()->is_black()) {
                    brother->set_color(node_type::Color::Red);
                    node = node->parent();
                } else {
                    if (brother->left_child()->is_black()) {
                        brother->right_child()->set_color(node_type::Color::Black);
                        brother->set_color(node_type::Color::Red);
                        rotate<RotateType::Left>(brother);
                        brother = node->parent()->left_child();
                    }
                    brother->set_color(node->parent()->color());
                    node->parent()->set_color(node_type::Color::Black);
                    brother->left_child()->set_color(node_type::Color::Black);
                    rotate<RotateType::Right>(node->parent());
                    node = m_root;
                }
            }
        }

        m_root->set_color(node_type::Color::Black);
    }

    enum class RotateType {
        Left,
        Right,
    };
    template <RotateType rt>
    constexpr void rotate(node_pointer node)
    {
        if constexpr (rt == RotateType::Left) {
            //    p <-(node)         b
            //  a   b      ---->   p   d
            //     c d            c a
            if (node->right_child()->is_nil_node()) {
                return;
            }

            node_pointer node_p = node;
            node_pointer node_a = node->left_child();
            node_pointer node_b = node->right_child();
            node_pointer node_c = node_b->left_child();

            node_b->set_left_child(node_p);
            node_b->set_parent(node_p->parent());
            if (node_p->parent()) {
                if (is_left_child(node_p)) {
                    node_p->parent()->set_left_child(node_b);
                } else {
                    node_p->parent()->set_right_child(node_b);
                }
            }
            node_p->set_parent(node_b);

            node_p->set_right_child(node_c);
            node_c->set_parent(node_p);

            if (is_root(node_p)) {
                m_root = node_b;
            }
        } else if constexpr (rt == RotateType::Right) {
            //    p <-(node)         a
            //  a   b      ---->   c   p
            // c d                    d b
            if (node->left_child()->is_nil_node()) {
                return;
            }

            node_pointer node_p = node;
            node_pointer node_a = node->left_child();
            node_pointer node_c = node_a->left_child();
            node_pointer node_d = node_a->right_child();

            node_a->set_right_child(node_p);
            node_a->set_parent(node_p->parent());
            if (node_p->parent()) {
                if (is_left_child(node_p)) {
                    node_p->parent()->set_left_child(node_a);
                } else {
                    node_p->parent()->set_right_child(node_a);
                }
            }
            node_p->set_parent(node_a);

            node->set_left_child(node_d);
            node_d->set_parent(node_p);

            if (is_root(node_p)) {
                m_root = node_a;
            }
        }
    }

    node_pointer _btree_insert(value_type&& value)
    {
        node_pointer new_node = m_node_allocator.allocate(1);
        if (!new_node) {
            return nullptr;
        }

        std::construct_at(new_node, std::move(value));
        new_node->set_parent(nil_node());
        new_node->set_left_child(nil_node());
        new_node->set_right_child(nil_node());

        if (m_root->is_nil_node()) {
            m_root = new_node;
            return new_node;
        }

        node_pointer cur_node = m_root;
        while (cur_node) {
            if (m_comp(new_node->value(), cur_node->value())) {
                if (cur_node->left_child()->is_nil_node()) {
                    new_node->set_parent(cur_node);
                    cur_node->set_left_child(new_node);
                    return new_node;
                }
                cur_node = cur_node->left_child();
            } else {
                if (cur_node->right_child()->is_nil_node()) {
                    new_node->set_parent(cur_node);
                    cur_node->set_right_child(new_node);
                    return new_node;
                }
                cur_node = cur_node->right_child();
            }
        }
        return nullptr;
    }

    node_pointer _btree_find(const_reference value)
    {
        node_pointer cur_node = m_root;
        while (cur_node && !cur_node->is_nil_node()) {
            if (cur_node->value() == value) {
                return cur_node;
            }

            if (m_comp(value, cur_node->value())) {
                cur_node = cur_node->left_child();
            } else {
                cur_node = cur_node->right_child();
            }
        }
        return nullptr;
    }

    node_pointer _btree_left_most(node_pointer node)
    {
        if (!node) {
            return nullptr;
        }
        if (node->is_nil_node()) {
            return nil_node();
        }

        while (!node->left_child()->is_nil_node()) {
            node = node->left_child();
        }
        return node;
    }

    node_pointer nil_node() const { return (node_pointer)&m_nil_node; }

    node_pointer m_root { nullptr };
    size_type m_size { 0 };
    key_compare m_comp {};
    allocator_type m_allocator {};
    node_alloc_type m_node_allocator {};
    node_type m_nil_node;
};

}

signed main()
{
    custom::set<int> the_set;
    std::ofstream file;
    // use dot -Tpng set.dot -o set.png to gen pic.
    file.open("set.dot");
    the_set.insert(30);
    the_set.insert(20);
    the_set.insert(10);
    the_set.insert(40);
    the_set.insert(50);
    the_set.insert(6);
    the_set.insert(7);
    the_set.insert(8);
    the_set.erase(7);
    the_set.erase(6);
    the_set.erase(30);
    the_set.insert(100);
    the_set.insert(33);
    the_set.insert(47);
    the_set.insert(7);
    the_set.erase(50);
    the_set.erase(100);
    the_set.insert(50);
    the_set.insert(100);
    the_set.erase(47);
    the_set.erase(50);
    the_set.print(file);
    file.close();
    return 0;
}