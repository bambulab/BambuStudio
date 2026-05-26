import type { HotendRackStatus } from '../types';

import rackReadNozzleUrl from '../assets/rack_read_nozzle.svg';
import rackReadHolderUrl from '../assets/rack_read_holder.svg';
import rackReadArrowLineUrl from '../assets/rack_read_arrow_line.svg';
import rackReadArrowHeadUrl from '../assets/rack_read_arrow_head.svg';

import rackTakeToolheadBodyUrl from '../assets/rack_take_toolhead_body.svg';
import rackTakeToolheadShellUrl from '../assets/rack_take_toolhead_shell.svg';
import rackTakeTubeDarkUrl from '../assets/rack_take_tube_dark.svg';
import rackTakeTubeLightUrl from '../assets/rack_take_tube_light.svg';
import rackTakeNozzleUrl from '../assets/rack_take_nozzle.svg';
import rackTakeLowerTubeUrl from '../assets/rack_take_lower_tube.svg';
import rackTakeLeftBlockUrl from '../assets/rack_take_left_block.svg';
import rackTakeRightBlockUrl from '../assets/rack_take_right_block.svg';
import rackTakeGreenArcUrl from '../assets/rack_take_green_arc.svg';

export function HotendRackProgressVisual({ operation }: { operation?: HotendRackStatus['operation'] }) {
  const isTakeFlow = operation === 'take' || operation === 'place' || operation === 'exchange';
  return (
    <div className="hotend-rack-progress-visual-card" aria-hidden="true">
      {isTakeFlow ? <TakeFlowVisual /> : <ReadFlowVisual />}
    </div>
  );
}

function ReadFlowVisual() {
  return (
    <>
      <img className="rack-visual-read-nozzle" src={rackReadNozzleUrl} alt="" />
      <img className="rack-visual-read-holder" src={rackReadHolderUrl} alt="" />
      <img className="rack-visual-read-line" src={rackReadArrowLineUrl} alt="" />
      <img className="rack-visual-read-head" src={rackReadArrowHeadUrl} alt="" />
    </>
  );
}

function TakeFlowVisual() {
  return (
    <div className="rack-visual-toolhead">
      <img className="rack-visual-take-body" src={rackTakeToolheadBodyUrl} alt="" />
      <img className="rack-visual-take-shell" src={rackTakeToolheadShellUrl} alt="" />
      <img className="rack-visual-take-tube-dark top" src={rackTakeTubeDarkUrl} alt="" />
      <img className="rack-visual-take-tube-light top" src={rackTakeTubeLightUrl} alt="" />
      <img className="rack-visual-take-nozzle" src={rackTakeNozzleUrl} alt="" />
      <img className="rack-visual-take-tube-dark bottom" src={rackTakeTubeDarkUrl} alt="" />
      <img className="rack-visual-take-tube-light bottom" src={rackTakeLowerTubeUrl} alt="" />
      <img className="rack-visual-take-left-block" src={rackTakeLeftBlockUrl} alt="" />
      <img className="rack-visual-take-right-block" src={rackTakeRightBlockUrl} alt="" />
      <img className="rack-visual-take-arc" src={rackTakeGreenArcUrl} alt="" />
    </div>
  );
}
