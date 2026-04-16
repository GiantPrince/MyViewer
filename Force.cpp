//https://github.com/savant117/avbd-demo3d/blob/main/source/force.cpp

#include "solver.hpp"

Force::Force(Solver* solver, Rigid* bodyA, Rigid* bodyB)
    : solver(solver), bodyA(bodyA), bodyB(bodyB), nextA(0), nextB(0)
{    
    next = solver->forces;
    solver->forces = this;
    
    if (bodyA != nullptr) {
        nextA = bodyA->forces;
        bodyA->forces = this;
    }
    if (bodyB != nullptr) {
        nextB = bodyB->forces;
        bodyB->forces = this;
    }
}


Force::~Force()
{
    // Remove from solver linked list
    Force** p = &solver->forces;
    while (*p != this)
        p = &(*p)->next;
    *p = next;

    // Remove from body linked lists
    if (bodyA)
    {
        p = &bodyA->forces;
        while (*p != this)
            p = (*p)->bodyA == bodyA ? &(*p)->nextA : &(*p)->nextB;
        *p = nextA;
    }

    if (bodyB)
    {
        p = &bodyB->forces;
        while (*p != this)
            p = (*p)->bodyA == bodyB ? &(*p)->nextA : &(*p)->nextB;
        *p = nextB;
    }
}
