import assert from 'node:assert/strict';
import { colorIdentityKey, resolveCandidateForSpool, candidateMatchesFormState, paletteForFilament } from '../src/features/filament-manager/colors/candidate';
import { cssBackgroundFor, TRANSPARENT_CHECKERED_BG } from '../src/features/filament-manager/colors/render';
import type { CandidateColor, Spool } from '../src/features/filament-manager/types';
const clear: CandidateColor = {name:'Clear',color_code:'32101',colors:['#00000000'],color_type:2};
const white: CandidateColor = {name:'Ivory White',color_code:'11100',colors:['#FFFFFFFF'],color_type:2};
const black: CandidateColor = {name:'Black',color_code:'10101',colors:['#000000FF'],color_type:2};
const multi: CandidateColor = {name:'Dual',color_code:'13906',colors:['#FF0000FF','#0000FFFF'],color_type:1};
const spool=(brand:string,setting_id:string,color_code:string,colors:string[]=[]):Spool=>({brand,setting_id,color_code,colors,color_name:'',color_type:2} as Spool);
const cache={GFG01:[clear],GFA01:[white,black,multi]};
for(const palette of [cache,{GFA01:[...cache.GFA01].reverse(),GFG01:[clear]}]) {
 for(const hex of ['#000000','#FFFFFF','#FFFF00']) {
  assert.equal(resolveCandidateForSpool(spool('Elegoo','custom',hex),palette),null);
  assert.equal(resolveCandidateForSpool(spool('ELEGOO','GFA01',hex),palette),null);
 }
}
assert.equal(resolveCandidateForSpool(spool('Bambu Lab','GFG01','#00000000'),cache),clear);
assert.equal(resolveCandidateForSpool(spool(' bambu lab ','GFA01','#000000'),cache),black);
assert.equal(resolveCandidateForSpool(spool('Bambu Lab','GFA01','#ffffff'),cache),white);
for(const [id,hex] of [['GFG01','#FFFFFF'],['GFG01','#000000FF'],['missing','#FFFFFF']])
 assert.equal(resolveCandidateForSpool(spool('Bambu Lab',id,hex),cache),null);
assert.equal(colorIdentityKey(' aabbcc '),'AABBCCFF');
assert.equal(colorIdentityKey('#aabbcc80'),'AABBCC80');
for(const bad of ['', '#123','#123456garbage','zzzzzz']) assert.equal(colorIdentityKey(bad),'');
assert.notEqual(colorIdentityKey('#000000'),colorIdentityKey('#00000000'));
assert.notEqual(colorIdentityKey('#FFFFFF80'),colorIdentityKey('#FFFFFFFF'));
assert.equal(candidateMatchesFormState({candidate:clear,formColorCode:'#000000FF',formColors:[]}),false);
assert.equal(candidateMatchesFormState({candidate:black,formColorCode:'#000000',formColors:[]}),true);
assert.equal(candidateMatchesFormState({candidate:clear,formColorCode:'#00000000',formColors:[]}),true);
assert.equal(candidateMatchesFormState({candidate:multi,formColorCode:'#0000FF',formColors:['#0000FF','#FF0000']}),true);
assert.equal(candidateMatchesFormState({candidate:multi,formColorCode:'#0000FF',formColors:['#0000FF00','#FF0000']}),false);
assert.equal(resolveCandidateForSpool(spool('Bambu Lab','GFA01','#FF0000',['#0000FF','#FF0000']),cache),multi);
assert.equal(resolveCandidateForSpool(spool('Bambu Lab','GFG01','#FF0000',['#0000FF','#FF0000']),cache),null);
assert.equal(resolveCandidateForSpool(spool('Bambu Lab','GFA01','#FF0000'),cache),null);
for(const [brand,sameFamily] of [['Elegoo',true],['Elegoo',false],['Bambu Lab',false]] as const) {
 const chosen=paletteForFilament([clear,white,multi],brand,sameFamily);
 assert.deepEqual(chosen.map(c=>c.name),['','','']);
 assert.deepEqual(chosen.map(c=>c.color_code),['#00000000','#FFFFFFFF','#FF0000FF']);
 assert.deepEqual(chosen[2].colors,multi.colors);
 const reopened=JSON.parse(JSON.stringify({...spool(brand,'custom',chosen[1].colors[0]),color_name:chosen[1].name}));
 assert.equal(reopened.color_name,'');
 assert.equal(resolveCandidateForSpool(reopened,cache),null);
}
assert.equal(paletteForFilament([white],'Bambu Lab',true)[0],white);
assert.equal(white.name,'Ivory White');
const named={...spool('Elegoo','custom','#FFFFFF'),color_name:'My white'};
const before=JSON.stringify(named);
assert.equal(resolveCandidateForSpool(named,cache),null);
assert.equal(JSON.stringify(named),before);
assert.equal(cssBackgroundFor({primaryHex:'#00000000'}),TRANSPARENT_CHECKERED_BG);
assert.notEqual(cssBackgroundFor({primaryHex:'#000000FF'}),TRANSPARENT_CHECKERED_BG);
console.log('Filament color identity regression tests passed');
