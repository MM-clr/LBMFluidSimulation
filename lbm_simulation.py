"""
LBM (Lattice Boltzmann Method) Fluid Simulation
================================================
D2Q9 model with BGK collision operator

Simulates lid-driven cavity flow:
  - Top wall moves to the right at a constant velocity (u_lid)
  - Left, right, and bottom walls are stationary (no-slip)

Usage:
  python lbm_simulation.py

Output:
  Saves a velocity magnitude plot and streamline plot at each snapshot
  interval into the output directory.
"""

import argparse
import os

import matplotlib.pyplot as plt
import numpy as np

# ============================================================
# D2Q9 Lattice Constants
# ============================================================
# Velocity directions (index → (ex, ey)):
#   6  2  5
#   3  0  1
#   7  4  8
EX = np.array([0,  1,  0, -1,  0,  1, -1, -1,  1], dtype=float)
EY = np.array([0,  0,  1,  0, -1,  1,  1, -1, -1], dtype=float)

# Lattice weights
W = np.array([4/9, 1/9, 1/9, 1/9, 1/9, 1/36, 1/36, 1/36, 1/36])

# Opposite direction lookup (for bounce-back)
OPP = [0, 3, 4, 1, 2, 7, 8, 5, 6]

# Number of velocity directions
Q = 9


# ============================================================
# LBM Functions
# ============================================================

def equilibrium(rho: np.ndarray, ux: np.ndarray, uy: np.ndarray) -> np.ndarray:
    """Compute the discrete equilibrium distribution function.

    This is the standard D2Q9 second-order expansion that approximates the
    Maxwell-Boltzmann distribution in the continuous, low-Mach-number limit.

    Args:
        rho: Density field  [Ny, Nx]
        ux:  x-velocity field [Ny, Nx]
        uy:  y-velocity field [Ny, Nx]

    Returns:
        feq: Equilibrium distribution [Q, Ny, Nx]
    """
    usq = ux**2 + uy**2
    feq = np.empty((Q, rho.shape[0], rho.shape[1]))
    for i in range(Q):
        eu = EX[i] * ux + EY[i] * uy
        feq[i] = W[i] * rho * (1.0 + 3.0*eu + 4.5*eu**2 - 1.5*usq)
    return feq


def macroscopic(f: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Compute macroscopic density and velocity from distribution functions.

    Args:
        f: Distribution function [Q, Ny, Nx]

    Returns:
        rho: Density field [Ny, Nx]
        ux:  x-velocity field [Ny, Nx]
        uy:  y-velocity field [Ny, Nx]
    """
    rho = f.sum(axis=0)
    ux = (EX @ f.reshape(Q, -1)).reshape(f.shape[1:]) / rho
    uy = (EY @ f.reshape(Q, -1)).reshape(f.shape[1:]) / rho
    return rho, ux, uy


def zou_he_moving_lid(f: np.ndarray, u_lid: float) -> np.ndarray:
    """Apply Zou-He velocity boundary condition at the moving top lid (y=-1).

    The lid moves in the +x direction at speed u_lid; uy = 0 at the lid.
    Sets the unknown incoming distributions f[4], f[7], f[8] at y = Ny-1.

    Derivation:
      At y = Ny-1 the known distributions are {0,1,2,3,5,6}.
      From mass conservation (uy=0) → ρ = f[0]+f[1]+f[3]+2*(f[2]+f[5]+f[6])
      From momentum conditions:
        f[4] = f[2]
        f[7] = f[5] + ½(f[1]−f[3]) − ½ρ·u_lid
        f[8] = f[6] − ½(f[1]−f[3]) + ½ρ·u_lid

    Args:
        f:     Distribution function [Q, Ny, Nx]
        u_lid: Lid velocity in x-direction (lattice units)

    Returns:
        f with the lid boundary applied.
    """
    rho_top = (f[0, -1, :] + f[1, -1, :] + f[3, -1, :]
               + 2.0 * (f[2, -1, :] + f[5, -1, :] + f[6, -1, :]))

    f[4, -1, :] = f[2, -1, :]
    f[7, -1, :] = (f[5, -1, :]
                   + 0.5 * (f[1, -1, :] - f[3, -1, :])
                   - 0.5 * rho_top * u_lid)
    f[8, -1, :] = (f[6, -1, :]
                   - 0.5 * (f[1, -1, :] - f[3, -1, :])
                   + 0.5 * rho_top * u_lid)
    return f


# ============================================================
# Simulation
# ============================================================

def run_simulation(Nx: int = 200, Ny: int = 100,
                   Re: float = 400, u_lid: float = 0.1,
                   n_steps: int = 20000, snapshot_interval: int = 1000,
                   output_dir: str = "output", animate: bool = False):
    """Run the lid-driven cavity LBM simulation.

    Algorithm per time step (collision-first):
      1. Apply Zou-He BC at top lid  (modifies f at y=Ny-1)
      2. Compute macroscopic quantities ρ, u
      3. BGK collision: f* = f − (f − feq) / τ
      4. Stream:  f_new[i](x+e_i) = f*[i](x)   (periodic roll)
      5. Bounce-back at stationary walls using f*  (replaces periodic values)

    Args:
        Nx:               Number of grid cells in x-direction
        Ny:               Number of grid cells in y-direction
        Re:               Reynolds number
        u_lid:            Lid velocity (lattice units, should be << 1 for stability)
        n_steps:          Total number of time steps
        snapshot_interval: Save a snapshot every this many steps
        output_dir:       Directory to save snapshots
        animate:          If True, show live animation (requires display)
    """
    # Derived parameters
    nu = u_lid * Ny / Re      # Kinematic viscosity (lattice units)
    tau = 3.0 * nu + 0.5     # BGK relaxation time (must be > 0.5)

    print("=== LBM Lid-Driven Cavity Simulation ===")
    print(f"  Grid:          {Nx} x {Ny}")
    print(f"  Reynolds No.:  {Re}")
    print(f"  Lid velocity:  {u_lid}")
    print(f"  Viscosity nu:  {nu:.6f}")
    print(f"  Relaxation τ:  {tau:.6f}")
    print(f"  Steps:         {n_steps}")
    print()

    if tau <= 0.5:
        raise ValueError(
            f"tau = {tau:.4f} ≤ 0.5 is numerically unstable. "
            "Reduce Re or u_lid, or increase Ny."
        )

    os.makedirs(output_dir, exist_ok=True)

    # ── Initialization ─────────────────────────────────────
    rho = np.ones((Ny, Nx))
    ux  = np.zeros((Ny, Nx))
    uy  = np.zeros((Ny, Nx))

    f = equilibrium(rho, ux, uy)

    # ── Figure setup ───────────────────────────────────────
    fig, axes = plt.subplots(1, 2, figsize=(14, 5))

    def save_snapshot(step: int, ux: np.ndarray, uy: np.ndarray,
                      rho: np.ndarray) -> None:
        speed = np.sqrt(ux**2 + uy**2)

        ax1, ax2 = axes
        ax1.clear()
        ax2.clear()

        # Velocity magnitude heat-map
        im1 = ax1.imshow(speed, origin="lower", cmap="inferno",
                         vmin=0, vmax=u_lid * 1.2)
        ax1.set_title(f"Velocity Magnitude  (step {step})")
        ax1.set_xlabel("x")
        ax1.set_ylabel("y")
        plt.colorbar(im1, ax=ax1, label="|u|")

        # Streamlines
        x = np.arange(Nx)
        y = np.arange(Ny)
        ax2.streamplot(x, y, ux, uy, density=1.5, color=speed,
                       cmap="viridis", linewidth=0.7, arrowsize=0.8)
        ax2.set_title(f"Streamlines  (step {step})")
        ax2.set_xlabel("x")
        ax2.set_ylabel("y")
        ax2.set_xlim(0, Nx - 1)
        ax2.set_ylim(0, Ny - 1)
        ax2.set_aspect("equal")

        plt.suptitle(
            f"LBM Lid-Driven Cavity  |  Re={Re}  |  u_lid={u_lid}",
            fontsize=13
        )
        plt.tight_layout(pad=2.5)

        path = os.path.join(output_dir, f"snapshot_{step:06d}.png")
        plt.savefig(path, dpi=120, bbox_inches="tight")
        print(f"  Saved {path}")

        if animate:
            plt.pause(0.001)

    # ── Main time loop ─────────────────────────────────────
    for step in range(1, n_steps + 1):

        # 1. Zou-He BC at moving top lid (BEFORE collision)
        f = zou_he_moving_lid(f, u_lid)

        # 2. Macroscopic quantities
        rho, ux, uy = macroscopic(f)

        # 3. BGK Collision
        feq = equilibrium(rho, ux, uy)
        f_post = f - (f - feq) / tau   # post-collision distribution

        # 4. Streaming (periodic roll)
        f = np.empty_like(f_post)
        for i in range(Q):
            f[i] = np.roll(np.roll(f_post[i], int(EX[i]), axis=1),
                           int(EY[i]), axis=0)

        # 5. Half-way bounce-back at stationary walls (using f_post)
        #    Bottom wall (y = 0)
        f[2,  0, :] = f_post[4,  0, :]   # up   ← down
        f[5,  0, :] = f_post[7,  0, :]   # ↗    ← ↙
        f[6,  0, :] = f_post[8,  0, :]   # ↖    ← ↘
        #    Left wall (x = 0)
        f[1,  :, 0] = f_post[3,  :, 0]   # right ← left
        f[5,  :, 0] = f_post[7,  :, 0]   # ↗    ← ↙
        f[8,  :, 0] = f_post[6,  :, 0]   # ↘    ← ↖
        #    Right wall (x = Nx-1)
        f[3,  :, -1] = f_post[1,  :, -1]  # left  ← right
        f[6,  :, -1] = f_post[8,  :, -1]  # ↖    ← ↘
        f[7,  :, -1] = f_post[5,  :, -1]  # ↙    ← ↗

        # 6. Snapshot
        if step % snapshot_interval == 0 or step == 1:
            speed_max = float(np.max(np.sqrt(ux**2 + uy**2)))
            print(f"Step {step:6d}/{n_steps}  "
                  f"max|u|={speed_max:.5f}  "
                  f"rho_avg={float(rho.mean()):.5f}")
            save_snapshot(step, ux, uy, rho)

    print("\nSimulation complete.")
    plt.close(fig)
    return ux, uy, rho


# ============================================================
# Entry Point
# ============================================================

def main():
    parser = argparse.ArgumentParser(
        description="LBM Lid-Driven Cavity Fluid Simulation (D2Q9 / BGK)"
    )
    parser.add_argument("--Nx",       type=int,   default=200,
                        help="Grid width  (default: 200)")
    parser.add_argument("--Ny",       type=int,   default=100,
                        help="Grid height (default: 100)")
    parser.add_argument("--Re",       type=float, default=400,
                        help="Reynolds number (default: 400)")
    parser.add_argument("--u_lid",    type=float, default=0.1,
                        help="Lid velocity in lattice units (default: 0.1)")
    parser.add_argument("--steps",    type=int,   default=20000,
                        help="Number of time steps (default: 20000)")
    parser.add_argument("--interval", type=int,   default=1000,
                        help="Snapshot interval (default: 1000)")
    parser.add_argument("--output",   type=str,   default="output",
                        help="Output directory (default: output)")
    parser.add_argument("--animate",  action="store_true",
                        help="Show live animation (requires display)")
    args = parser.parse_args()

    run_simulation(
        Nx=args.Nx,
        Ny=args.Ny,
        Re=args.Re,
        u_lid=args.u_lid,
        n_steps=args.steps,
        snapshot_interval=args.interval,
        output_dir=args.output,
        animate=args.animate,
    )


if __name__ == "__main__":
    main()
