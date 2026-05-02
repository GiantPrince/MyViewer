//https://github.com/savant117/avbd-demo3d/blob/main/source/solver.cpp
#include "Timer.hpp"
#include "solver.hpp"

#include <iostream>

Solver::Solver()
    : bodies(0), forces(0), num_bodies(0)
{
    dt = 1.0f / 60.0f;
    gravity = -10.0f;
    iterations = 10;

    betaLin = 10000.0f;
    betaAng = 100.0f;

    alpha = 0.99f;
    gamma = 0.999f;
}

Solver::~Solver()
{
    clear();
}

void Solver::clear()
{
    while (forces)
        delete forces;

    while (bodies)
        delete bodies;
}


void Solver::step()
{
    Timer steptimer([&](double ms) {
        std::cout << "Step: " << ms * 1000.0 << " ms" << std::endl;
        });
    // broadphase collision detection    
    {
        Timer timer([&](double ms) {
            std::cout << "Broadphase: " << ms * 1000.0 << " ms" << std::endl;
            });
        for (Rigid* bodyA = bodies; bodyA != 0; bodyA = bodyA->next)
        {
            for (Rigid* bodyB = bodyA->next; bodyB != 0; bodyB = bodyB->next)
            {
                float3 dp = bodyA->positionLin - bodyB->positionLin;
                float r = bodyA->radius + bodyB->radius;
                if (dot(dp, dp) <= r * r && !bodyA->constrainedTo(bodyB))
                    new Manifold(this, bodyA, bodyB);
            }
        }

    }
    
    // Initialize and warmstart forces
    {
        Timer timer([&](double ms) {
            std::cout << "Init: " << ms * 1000.0 << " ms" << std::endl;
            });
        for (Force* force = forces; force != 0;)
        {
            if (!force->initialize())
            {
                Force* next = force->next;
                delete force;
                force = next;
            }
            else {
                force = force->next;
            }
        }
    }
        
    // Initialize and warmstart
    {
        Timer timer([&](double ms) {
            std::cout << "Warmstart: " << ms * 1000.0 << " ms" << std::endl;
            });
        for (Rigid* body = bodies; body != 0; body = body->next)
        {
            body->inertialLin = body->positionLin + body->velocityLin * dt;
            if (!body->isStatic)
                body->inertialLin += float3{ 0, gravity, 0 } * (dt * dt);
            body->inertialAng = body->positionAng + body->velocityAng * dt;
            
            float3 accel = (body->velocityLin - body->prevVelocityLin) / dt;
            float accelExt = accel.y * sign(gravity);
            float accelWeight = clamp(accelExt / abs(gravity), 0.0f, 1.0f);
            if (!isfinite(accelWeight))
                accelWeight = 0.0f;
            
            body->initialLin = body->positionLin;
            body->initialAng = body->positionAng;
            if (!body->isStatic)
            {
                body->positionLin = body->positionLin + body->velocityLin * dt + float3{ 0, gravity, 0 } * (accelWeight * dt * dt);
                body->positionAng = body->positionAng + body->velocityAng * dt;
            }
        }

    }
    
    {
        Timer timer([&](double ms) {
            std::cout << "Main_iteration: " << ms * 1000.0 << " ms" << std::endl;
            });        
        for (int it = 0; it < iterations; it++)
        {                        
            for (Rigid* body = bodies; body != 0; body = body->next)
            {
               
                if (body->isStatic)
                    continue;
                
                float3x3 MLin = diagonal(body->mass, body->mass, body->mass);
                float3x3 MAng = diagonal(body->moment.x, body->moment.y, body->moment.z);

                float3x3 lhsLin = MLin / (dt * dt);
                float3x3 lhsAng = MAng / (dt * dt);
                float3x3 lhsCross = float3x3{ 0, 0, 0, 0, 0, 0, 0, 0, 0 };

                float3 rhsLin = MLin / (dt * dt) * (body->positionLin - body->inertialLin);
                float3 rhsAng = MAng / (dt * dt) * (body->positionAng - body->inertialAng);
                int n = 0;
                for (Force* force = body->forces; force != 0; force = (force->bodyA == body) ? force->nextA : force->nextB) { 
                    n++;
                    force->updatePrimal(body, alpha, lhsLin, lhsAng, lhsCross, rhsLin, rhsAng);                    
                }               
                if (n > 10) {
                    n = 0;
                }
                
                float3 dxLin, dxAng;
                solve(lhsLin, lhsAng, lhsCross, -rhsLin, -rhsAng, dxLin, dxAng);

                body->positionLin = body->positionLin + dxLin;
                body->positionAng = body->positionAng + dxAng;
            }
            
            for (Force* force = forces; force != 0; force = force->next) {
                force->updateDual(alpha);
            }
        }

    }
        
    {
        Timer timer([&](double ms) {
            std::cout << "Velocity_Update: " << ms * 1000.0 << " ms" << std::endl;
            });
        for (Rigid* body = bodies; body != 0; body = body->next) {
            body->prevVelocityLin = body->velocityLin;
            if (!body->isStatic) {
                body->velocityLin = (body->positionLin - body->initialLin) / dt;
                body->velocityAng = (body->positionAng - body->initialAng) / dt;
            }
        }
    }
    
}
