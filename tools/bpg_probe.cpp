#include "BreakPatternGenerator.h"
#include <cstdio>
int main(){ BreakPatternGenerator::Params p; p.sliceCount=108; p.baseNote=60; p.style=BreakPatternGenerator::Style::Random; auto s=BreakPatternGenerator::generate(p); auto n=BreakPatternGenerator::asNotes(p,s,60); int mx=0; for(auto&x:n) if(x.pitch>mx) mx=x.pitch; printf("notes=%d maxPitch=%d
",(int)n.size(),mx); return (mx>127)?2:0; }
