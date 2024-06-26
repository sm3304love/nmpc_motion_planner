#pragma once
#include <Eigen/Dense>
#include <casadi/casadi.hpp>
#include <memory>
#include <vector>

namespace casadi_mpc_template
{

template <class T> static T integrate_dynamics_forward_euler(double dt, T x, T u, std::function<T(T, T)> dynamics)
{
    return x + dt * dynamics(x, u);
}

template <class T> static T integrate_dynamics_modified_euler(double dt, T x, T u, std::function<T(T, T)> dynamics)
{
    T k1 = dynamics(x, u);
    T k2 = dynamics(x + dt * k1, u);

    return x + dt * (k1 + k2) / 2;
}

template <class T> static T integrate_dynamics_rk4(double dt, T x, T u, std::function<T(T, T)> dynamics)
{
    T k1 = dynamics(x, u);
    T k2 = dynamics(x + dt / 2 * k1, u);
    T k3 = dynamics(x + dt / 2 * k2, u);
    T k4 = dynamics(x + dt * k3, u);
    return x + dt / 6 * (k1 + 2 * k2 + 2 * k3 + k4);
}

class Problem
{
  public:
    enum class DynamicsType
    {
        ContinuesForwardEuler,
        ContinuesModifiedEuler,
        ContinuesRK4,
        Discretized,
    };

    enum class ConstraintType
    {
        Equality,
        Inequality
    };

    Problem(DynamicsType dyn_type, size_t _nx, size_t _nu, size_t _horizon, double _dt)
        : dyn_type_(dyn_type), nx_(_nx), nu_(_nu), horizon_(_horizon), dt_(_dt)
    {
        double inf = std::numeric_limits<double>::infinity();

        Eigen::VectorXd uub = Eigen::VectorXd::Constant(nu(), inf);
        Eigen::VectorXd ulb = -uub;
        u_bounds_ = std::vector<LUbound>{horizon(), {ulb, uub}};

        Eigen::VectorXd xub = Eigen::VectorXd::Constant(nx(), inf);
        Eigen::VectorXd xlb = -xub;
        x_bounds_ = std::vector<LUbound>{horizon(), {xlb, xub}};
    }

    virtual casadi::MX dynamics(casadi::MX x, casadi::MX u) = 0;

    void set_input_bound(Eigen::VectorXd lb, Eigen::VectorXd ub, int start = -1, int end = -1)
    {
        std::tie(start, end) = index_range(start, end);
        for (int i = start; i < end; i++)
        {
            u_bounds_[i] = {lb, ub};
        }
    }

    void set_input_lower_bound(Eigen::VectorXd lb, int start = -1, int end = -1)
    {
        std::tie(start, end) = index_range(start, end);
        for (int i = start; i < end; i++)
        {
            u_bounds_[i].first = lb;
        }
    }

    void set_input_upper_bound(Eigen::VectorXd ub, int start = -1, int end = -1)
    {
        std::tie(start, end) = index_range(start, end);
        for (int i = start; i < end; i++)
        {
            u_bounds_[i].second = ub;
        }
    }

    void set_state_bound(Eigen::VectorXd lb, Eigen::VectorXd ub, int start = -1, int end = -1)
    {
        std::tie(start, end) = index_range(start, end);
        for (int i = start; i < end; i++)
        {
            x_bounds_[i] = {lb, ub};
        }
    }

    void set_state_lower_bound(Eigen::VectorXd lb, int start = -1, int end = -1)
    {
        std::tie(start, end) = index_range(start, end);
        for (int i = start; i < end; i++)
        {
            x_bounds_[i].first = lb;
        }
    }

    void set_state_upper_bound(Eigen::VectorXd ub, int start = -1, int end = -1)
    {
        std::tie(start, end) = index_range(start, end);
        for (int i = start; i < end; i++)
        {
            x_bounds_[i].second = ub;
        }
    }

    void add_constraint(ConstraintType type, std::function<casadi::MX(casadi::MX, casadi::MX)> constrinat)
    {
        if (type == ConstraintType::Equality)
        {
            equality_constrinats_.push_back(constrinat);
        }
        else
        {
            inequality_constrinats_.push_back(constrinat);
        }
    }

    virtual casadi::MX stage_cost(casadi::MX x, casadi::MX u)
    {
        return 0;
    }

    virtual casadi::MX terminal_cost(casadi::MX x)
    {
        return 0;
    }

    DynamicsType dynamics_type() const
    {
        return dyn_type_;
    }
    size_t nx() const
    {
        return nx_;
    }
    size_t nu() const
    {
        return nu_;
    }
    size_t horizon() const
    {
        return horizon_;
    }
    double dt() const
    {
        return dt_;
    }

  private:
    std::pair<int, int> index_range(int start, int end)
    {
        if (start == -1 && end == -1)
        {
            return {0, horizon_};
        }
        if (start != -1 && end == -1)
        {
            return {start, start + 1};
        }
        return {start, end};
    }

    DynamicsType dyn_type_;
    const size_t nx_;
    const size_t nu_;
    const size_t horizon_;
    const double dt_;

    using ConstraintFunc = std::function<casadi::MX(casadi::MX, casadi::MX)>;
    std::vector<ConstraintFunc> equality_constrinats_;
    std::vector<ConstraintFunc> inequality_constrinats_;

    using LUbound = std::pair<Eigen::VectorXd, Eigen::VectorXd>;
    std::vector<LUbound> u_bounds_;
    std::vector<LUbound> x_bounds_;

    friend class MPC;
};

class MPC
{
  public:
    static casadi::Dict default_config()
    {
        casadi::Dict config = {{"calc_lam_p", true},     {"calc_lam_x", true},  {"ipopt.sb", "yes"},
                               {"ipopt.print_level", 0}, {"print_time", false}, {"ipopt.warm_start_init_point", "yes"},
                               {"expand", true}};
        return config;
    }

    static casadi::Dict default_qpoases_config()
    {
        casadi::Dict config = {{"calc_lam_p", true},
                               {"calc_lam_x", true},
                               {"max_iter", 100},
                               {"print_header", false},
                               {"print_iteration", false},
                               {"print_status", false},
                               {"print_time", false},
                               {"qpsol", "qpoases"},
                               {"qpsol_options", casadi::Dict{{"enableRegularisation", true}, {"printLevel", "none"}}},
                               {"expand", true}};
        return config;
    }

    static casadi::Dict default_hpipm_config()
    {
        casadi::Dict config = {{"calc_lam_p", true},
                               {"calc_lam_x", true},
                               {"max_iter", 100},
                               {"print_header", false},
                               {"print_iteration", false},
                               {"print_status", false},
                               {"print_time", false},
                               {"qpsol", "hpipm"},
                               {"qpsol_options", casadi::Dict{{"hpipm.iter_max", 100}, {"hpipm.warm_start", true}}},
                               {"expand", true}};
        return config;
    }

    template <class T>
    MPC(std::shared_ptr<T> prob, std::string solver_name = "ipopt", casadi::Dict config = default_config())
        : prob_(prob), solver_name_(solver_name), config_(config)
    {
        using namespace casadi;
        static_assert(std::is_base_of_v<Problem, T>, "prob must be based SimpleProb");

        const size_t nx = prob_->nx();
        const size_t nu = prob_->nu();
        const size_t N = prob_->horizon();

        Xs.reserve(N + 1);
        Us.reserve(N);

        for (size_t i = 0; i < N; i++)
        {
            Xs.push_back(MX::sym("X_" + std::to_string(i), nx, 1));
            Us.push_back(MX::sym("U_" + std::to_string(i), nu, 1));
        }
        Xs.push_back(MX::sym("X_" + std::to_string(N), nx, 1));

        std::vector<MX> w, g;
        std::vector<DM> w0;
        MX J = 0;

        std::function<casadi::MX(casadi::MX, casadi::MX)> dynamics;
        switch (prob_->dynamics_type())
        {
        case Problem::DynamicsType::ContinuesForwardEuler: {
            std::function<casadi::MX(casadi::MX, casadi::MX)> con_dyn =
                std::bind(&Problem::dynamics, prob_, std::placeholders::_1, std::placeholders::_2);
            dynamics = std::bind(integrate_dynamics_forward_euler<casadi::MX>, prob_->dt(), std::placeholders::_1,
                                 std::placeholders::_2, con_dyn);
            break;
        }
        case Problem::DynamicsType::ContinuesModifiedEuler: {
            std::function<casadi::MX(casadi::MX, casadi::MX)> con_dyn =
                std::bind(&Problem::dynamics, prob_, std::placeholders::_1, std::placeholders::_2);
            dynamics = std::bind(integrate_dynamics_modified_euler<casadi::MX>, prob_->dt(), std::placeholders::_1,
                                 std::placeholders::_2, con_dyn);
            break;
        }
        case Problem::DynamicsType::ContinuesRK4: {
            std::function<casadi::MX(casadi::MX, casadi::MX)> con_dyn =
                std::bind(&Problem::dynamics, prob_, std::placeholders::_1, std::placeholders::_2);
            dynamics = std::bind(integrate_dynamics_rk4<casadi::MX>, prob_->dt(), std::placeholders::_1,
                                 std::placeholders::_2, con_dyn);
            break;
        }
        case Problem::DynamicsType::Discretized:
            dynamics = std::bind(&Problem::dynamics, prob_, std::placeholders::_1, std::placeholders::_2);
            break;
        }

        auto &u_bounds = prob_->u_bounds_;
        auto &x_bounds = prob_->x_bounds_;
        for (size_t i = 0; i < N; i++) // problem?
        {
            w.push_back(Xs[i]);

            if (i != 0)
            {
                for (auto l = 0; l < nx; l++)
                {
                    lbw_.push_back(x_bounds[i - 1].first[l]);
                    ubw_.push_back(x_bounds[i - 1].second[l]);
                }
            }
            else
            {
                for (auto l = 0; l < nx; l++)
                {
                    lbw_.push_back(0);
                    ubw_.push_back(0);
                }
            }

            w.push_back(Us[i]);
            for (auto l = 0; l < nu; l++)
            {
                lbw_.push_back(u_bounds[i].first[l]);
                ubw_.push_back(u_bounds[i].second[l]);
            }
            MX xplus = dynamics(Xs[i], Us[i]);
            J += prob_->stage_cost(Xs[i], Us[i]);

            g.push_back((xplus - Xs[i + 1]));
            for (auto l = 0; l < nx; l++)
            {
                lbg_.push_back(0);
                ubg_.push_back(0);
            }

            for (auto &con : prob_->equality_constrinats_)
            {
                auto con_val = con(Xs[i + 1], Us[i]);
                g.push_back(con_val);
                for (auto l = 0; l < con_val.size1(); l++)
                {
                    lbg_.push_back(0);
                    ubg_.push_back(0);
                }
            }
            for (auto &con : prob_->inequality_constrinats_)
            {
                auto con_val = con(Xs[i + 1], Us[i]);
                g.push_back(con_val);
                for (auto l = 0; l < con_val.size1(); l++)
                {
                    lbg_.push_back(-inf);
                    ubg_.push_back(0);
                }
            }
        }
        J += prob_->terminal_cost(Xs[N]);

        w.push_back(Xs[N]);

        for (auto l = 0; l < nx; l++)
        {
            lbw_.push_back(x_bounds[N - 1].first[l]);
            ubw_.push_back(x_bounds[N - 1].second[l]);
        }
        // std::cout << "lbw_ size: " << lbw_.size() << std::endl;
        casadi_prob_ = {{"x", vertcat(w)}, {"f", J}, {"g", vertcat(g)}};
        solver_ = nlpsol("solver", solver_name_, casadi_prob_, config_);
    }

    Eigen::VectorXd solve(Eigen::VectorXd x0)
    {
        using namespace casadi;
        const size_t nx = prob_->nx();
        const size_t nu = prob_->nu();
        const size_t N = prob_->horizon();
        // need to fix
        for (auto l = 0; l < nx; l++)
        {
            lbw_[l] = x0[l];
            ubw_[l] = x0[l];
        }

        // std::cout << "lbw_ size: " << lbw_.size() << std::endl;
        // std::cout << "ubw_ size: " << ubw_.size() << std::endl;
        // std::cout << "lbg_ size: " << lbg_.size() << std::endl;
        // std::cout << "ubg_ size: " << ubg_.size() << std::endl;
        // std::cout << "nx: " << nx << std::endl;

        DMDict arg;
        arg["x0"] = w0_;
        arg["lbx"] = vertcat(lbw_);
        arg["ubx"] = vertcat(ubw_);
        arg["lbg"] = vertcat(lbg_);
        arg["ubg"] = vertcat(ubg_);
        arg["lam_x0"] = lam_x0_;
        arg["lam_g0"] = lam_g0_;
        DMDict sol = solver_(arg);

        w0_ = sol["x"];
        lam_x0_ = sol["lam_x"];
        lam_g0_ = sol["lam_g"];

        Eigen::VectorXd opt_u(nu);
        std::copy(w0_.ptr() + nx, w0_.ptr() + nx + nu, opt_u.data());

        return opt_u;
    }

    casadi::MXDict casadi_prob() const
    {
        return casadi_prob_;
    }

  private:
    std::shared_ptr<Problem> prob_;
    std::string solver_name_;
    casadi::Dict config_;
    casadi::MXDict casadi_prob_;
    casadi::Function solver_;
    std::vector<casadi::MX> Xs;
    std::vector<casadi::MX> Us;

    std::vector<casadi::DM> lbw_;
    std::vector<casadi::DM> ubw_;
    std::vector<casadi::DM> lbg_;
    std::vector<casadi::DM> ubg_;

    casadi::DM w0_;
    casadi::DM lam_x0_;
    casadi::DM lam_g0_;
};

} // namespace casadi_mpc_template