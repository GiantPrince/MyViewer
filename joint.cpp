
#include "solver.hpp"

#include <assert.h>

inline float3x3 geometricStiffnessBallSocket(int k, float3 v)
{
    float3x3 m = diagonal(-v[k], -v[k], -v[k]);

    m[0][k] += v[0];
    m[1][k] += v[1];
    m[2][k] += v[2];

    return m;
}

Joint::Joint(Solver* solver, Rigid* bodyA, Rigid* bodyB, float3 rA, float3 rB, float stiffnessLin, float stiffnessAng, float fracture)
    : Force(solver, bodyA, bodyB), rA(rA), rB(rB), stiffnessLin(stiffnessLin), stiffnessAng(stiffnessAng), fracture(fracture), broken(false)
{
    this->penaltyLin = float3{ 0, 0, 0 }; 
    this->penaltyAng = float3{ 0, 0, 0 };
    this->lambdaLin = float3{ 0, 0, 0 }; 
    this->lambdaAng = float3{ 0, 0, 0 };
    this->torqueArm = lengthSq((bodyA ? bodyA->size : float3{ 0, 0, 0 }) + bodyB->size);
    
}

bool Joint::initialize()
{
    assert(bodyA != nullptr);
    C0Lin = transform(bodyA->positionLin, bodyA->positionAng, rA) - transform(bodyB->positionLin, bodyB->positionAng, rB);
    C0Ang = (bodyA->positionAng - bodyB->positionAng) * torqueArm;
    
    penaltyLin = clamp(penaltyLin * solver->gamma, PENALTY_MIN, PENALTY_MAX);
    penaltyAng = clamp(penaltyAng * solver->gamma, PENALTY_MIN, PENALTY_MAX);

    lambdaLin = lambdaLin * solver->alpha * solver->gamma;
    lambdaAng = lambdaAng * solver->alpha * solver->gamma;
        
    penaltyLin = min(penaltyLin, stiffnessLin);
    penaltyAng = min(penaltyAng, stiffnessAng);

    return !broken;
}

void Joint::updatePrimal(Rigid* body, float alpha, float3x3& lhsLin, float3x3& lhsAng, float3x3& lhsCross, float3& rhsLin, float3& rhsAng)
{    
    if (lengthSq(penaltyLin) > 0)
    {        
        float3x3 K = diagonal(penaltyLin.x, penaltyLin.y, penaltyLin.z);
        float3 C = transform(bodyA->positionLin, bodyA->positionAng, rA) - transform(bodyB->positionLin, bodyB->positionAng, rB);
        
        if (isinf(stiffnessLin))
            C -= C0Lin * alpha;
        
        float3 F = K * C + lambdaLin;
        
        float3x3 j = float3x3{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
        float3x3 jLin = body == bodyA ? j : -j;
        float3x3 jAng = body == bodyA ? skew(-rotate(bodyA->positionAng, rA)) : skew(rotate(bodyB->positionAng, rB));
       
        float3x3 jLinT = transpose(jLin);
        float3x3 jAngT = transpose(jAng);
        float3x3 jAngTk = jAngT * K;

        lhsLin += jLinT * K * jLin;
        lhsAng += jAngTk * jAng;
        lhsCross += jAngTk * jLin;
        
        float3 r = body == bodyA ? rotate(bodyA->positionAng, rA) : -rotate(bodyB->positionAng, rB);
        float3x3 H =
            geometricStiffnessBallSocket(0, r) * F[0] +
            geometricStiffnessBallSocket(1, r) * F[1] +
            geometricStiffnessBallSocket(2, r) * F[2];
        lhsAng += diagonalize(H);
        
        rhsLin += jLinT * F;
        rhsAng += jAngT * F;
    }
    
    if (lengthSq(penaltyAng) > 0)
    {        
        float3x3 K = diagonal(penaltyAng.x, penaltyAng.y, penaltyAng.z);
        float3 C = ((bodyA ? bodyA->positionAng : quat{ 0, 0, 0, 1 }) - bodyB->positionAng) * torqueArm;
        
        if (isinf(stiffnessAng))
            C -= C0Ang * alpha;
        
        float3 F = K * C + lambdaAng;
        
        float3x3 jAng = (body == bodyA ? float3x3{ 1, 0, 0, 0, 1, 0, 0, 0, 1 } : float3x3{ -1, 0, 0, 0, -1, 0, 0, 0, -1 }) * torqueArm;
        
        lhsAng += transpose(jAng) * K * jAng;
        
        rhsAng += transpose(jAng) * F;
    }
}

void Joint::updateDual(float alpha)
{    
    if (lengthSq(penaltyLin) > 0) {        
        float3x3 K = diagonal(penaltyLin.x, penaltyLin.y, penaltyLin.z);
        float3 C = (bodyA ? transform(bodyA->positionLin, bodyA->positionAng, rA) : rA) - transform(bodyB->positionLin, bodyB->positionAng, rB);

        if (isinf(stiffnessLin)) {            
            C -= C0Lin * alpha;
            float3 F = K * C + lambdaLin;            
            lambdaLin = F;
        }

       
        penaltyLin = min(penaltyLin + abs(C) * solver->betaLin, min(stiffnessLin, PENALTY_MAX));
    }

    
    if (lengthSq(penaltyAng) > 0) {
        
        float3x3 K = diagonal(penaltyAng.x, penaltyAng.y, penaltyAng.z);
        float3 C = ((bodyA ? bodyA->positionAng : quat{ 0, 0, 0, 1 }) - bodyB->positionAng) * torqueArm;

        if (isinf(stiffnessAng)) {
            
            C -= C0Ang * alpha;
           
            float3 F = K * C + lambdaAng;
            
            lambdaAng = F;
        }
        
        penaltyAng = min(penaltyAng + abs(C) * solver->betaAng, min(stiffnessAng, PENALTY_MAX));
    }
    
    // test if broken
    if (lengthSq(lambdaAng) > fracture * fracture)
    {
        lambdaLin = { 0, 0, 0 };
        lambdaAng = { 0, 0, 0 };
        penaltyLin = { 0, 0, 0 };
        penaltyAng = { 0, 0, 0 };
        
        broken = true;
    }
}