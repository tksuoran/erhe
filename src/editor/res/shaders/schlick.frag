L  = V'

V  = outcoming direction of light
L  = incoming direction of light
N  = surface normal vector
T  = surface tangent vector
H  = bisector of V and L
H_ = projection of H _|_ NdotV

t  = dot(H, N)
u  = dot(H, V)
v  = dot(V, N)
v' = dot(L, N)
w  = dot(T, H_)
alpha  = acos(t)
beta   = acos(u)
theta  = acos(v)
theta' = acos(v')
phi    = acos(w)

2.1 The Cook-Torrance Model

Equation 4:

R_lambda(alpha, beta, theta, theta`, phi) = d/Pi * C_lambda + s/(4Pivv') * F_lambda(beta) * G(theta, theta') * D(alpha, phi)
with d + s = 1

C_lambda         (0,1)   = surface diffuse relfectance at wavelength lambda
D(alpha,phi)     (0,inf) = microfacet slope distribution function
F_lambda(beta)   (0,1)   = fresnel factor
G(theta, theta') (0,1)   = geometrical attenuation (microfacet masking and shadowing)

Equation 5:

R_lambda(t, u, v, v') = d/Pi * D_lambda + s/(4Pivv') * F_lambda(u) * G(v, v') * D(t)

2.3 The Ward Model

Equation 10:

R_lambda(t,v,v',w) = d/Pi * C_lambda + s/(4Pisqrt(vv')) * C'_lambda * D(t,w)
with
D(t,w) = 1/(mn) * e**(t2-1)/t2 * (w2/m2 + 1-w2/n2)

5.1 Formutation

Equation 22:

R = S_(u) * D(t,v,v',w)

S(u) = C_lambda + (1 - C_lambda) * pow(1 - u, 5)

D(t,v,v',w) = 1 / (4 * Pi * v * v') * Z(t) * A(w)

Z(t) = r / (1 + r * t2 - t2)

A(w) = sqrt(p / (p2 - p2 * w2 + w2))

Equation 30:

D(t,v,v',w) = G(v)*G(v') / (4*Pi*v*v') * Z(t) * A(w) + (1 - G(v) * G(v')) / (4*Pi*v*v')

G(v)  = v  / (r - r*v + v)
G(v') = v' / (r - r*v' + v')


const float M_PI = 3.141592653589793;

        //float TdotH      = dot(h - NdotH * n, t);
        //float BdotH      = dot(h - NdotH * n, b);

float hn  = dot(H, N);                          // t Figure 1
float hv  = dot(H, V);                          // u
float vn  = dot(V, N);                          // v
float ln  = dot(L, N);                          // v'
float ht  = dot(H - hn * N, T);                 // w
vec3  S   = C + (1.0 - C) * pow(1.0 - vn, 5.0); // Equation 24 with u=v
float Gvn = vn / (r - r * vn + vn);             // Equation 31
float Gln = ln / (r - r * ln + vn);             // Equation 31
float hn2 = hn * hn;
float ht2 = ht * ht;
float Zhn = r / (1.0 + r * hn2 - hn2);          // Equation 28
float Aht = sqrt(p / (p2 - p2 * ht2 + ht2));    // Equation 28
float G   = Gvn * Gln;
float den = 4.0 * M_PI * vn * ln;
float D   = G / den * Zhn * Aht + (1 - G) / den
vec3  R   = S * D;


    // Schlick anisotropic for comparison
    if (false)
    {
        vec3  C   = material.base_color.rgb;
        float r   = 0.1;
        float p   = 1.0;
        float p2  = p * p;
        vec3  L   = l;
        vec3  N   = n;
        vec3  T   = t;
        vec3  V   = normalize(view_position_in_world - v_position.xyz);
        vec3  H   = normalize(l + v);

        float hn  = max(0.0, dot(H, N));                          // t Figure 1
        float hv  = max(0.0, dot(H, V));                          // u
        float vn  = max(0.0, dot(V, N));                          // v
        float ln  = max(0.0, dot(L, N));                          // v'
        float ht  = max(0.0, dot(H - hn * N, T));                 // w
        vec3  S   = C + (vec3(1.0) - C) * pow(1.0 - vn, 5.0); // Equation 24
        float Gvn = vn / (r - r * vn + vn);             // Equation 31
        float Gln = ln / (r - r * ln + vn);             // Equation 31
        float hn2 = hn * hn;
        float ht2 = ht * ht;
        float Zhn = r / (1.0 + r * hn2 - hn2);          // Equation 28
        float Aht = sqrt(p / (p2 - p2 * ht2 + ht2));    // Equation 28
        float G   = Gvn * Gln;
        float den = 4.0 * M_PI * vn * ln;
        float D   = G / den * Zhn * Aht; // + (1.0 - G) / den;
        out_color.rgb = S * Zhn * Aht;
        //out_color.rgb = vec3(pow(1.0 - vn, 5.0));
    }
