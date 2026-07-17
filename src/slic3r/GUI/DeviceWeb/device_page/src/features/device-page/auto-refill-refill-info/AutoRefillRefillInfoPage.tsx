import { useEffect, useRef } from 'react';
import type { RefObject } from 'react';
import { useTranslation } from 'react-i18next';
import { useAutoRefillRefillInfoBridge } from './useAutoRefillRefillInfoBridge';
import { useSendToCpp } from '../../../hooks/Bridge';
import { GroupColumn } from './components/GroupColumn';
import { resolveHostContentSize } from './hostContentSize';
import emptyRefillGroupImage from './assets/empty_refill_group.png';
import './auto-refill-refill-info.css';

const HOST_LAYOUT_MODULE = 'device_host';

// Observe the real rendered size of the page and report it to the wx host via
// device_host/layout/content_size so the dialog can size itself instead of
// relying only on the initial SetMinClientSize.
function useReportHostContentSize(rootRef: RefObject<HTMLElement | null>) {
  const send = useSendToCpp();
  const seqRef = useRef(1_000_000);

  useEffect(() => {
    const root = rootRef.current;
    if (!root) return;

    let rafId = 0;
    let lastSizeKey = '';

    const report = () => {
      rafId = 0;
      const { width, height } = resolveHostContentSize(root);
      if (width <= 0 || height <= 0) return;

      const sizeKey = `${width}x${height}`;
      if (sizeKey === lastSizeKey) return;
      lastSizeKey = sizeKey;

      send(
        { version: '1.0', type: 'request', seq: seqRef.current++, ts: Date.now() },
        {
          module: HOST_LAYOUT_MODULE,
          submod: 'layout',
          action: 'content_size',
          payload: { width, height },
        },
      );
    };

    const scheduleReport = () => {
      if (rafId) return;
      rafId = window.requestAnimationFrame(report);
    };

    scheduleReport();
    window.addEventListener('resize', scheduleReport);

    const observer = typeof ResizeObserver === 'function' ? new ResizeObserver(scheduleReport) : null;
    observer?.observe(root);

    return () => {
      if (rafId) window.cancelAnimationFrame(rafId);
      observer?.disconnect();
      window.removeEventListener('resize', scheduleReport);
    };
  }, [rootRef, send]);
}

export function AutoRefillRefillInfoPage() {
  const pageRef = useRef<HTMLDivElement>(null);
  const { t } = useTranslation();
  const { state, error } = useAutoRefillRefillInfoBridge();
  useReportHostContentSize(pageRef);

  const groups = state.extruders.flatMap((ext) => ext.groups);
  const groupCount = groups.length;
  // Presentation is C++-driven: text and affordances come from the view-model
  // rather than being hard-coded here (see Schema.hpp keys::root).
  const editable = state.editable ?? false;
  const description = state.description
    ?.split('\n')
    .map((line) => t(line))
    .join('\n');

  return (
    <div ref={pageRef} className="refill-page" data-testid="auto-refill-refill-info-page">
      <div className="refill-page__intro">
        <div className="refill-page__title">
          {t('Auto-refill management')}
          {groupCount > 0 ? `（${groupCount}）` : ''}
        </div>
        {description && <div className="refill-page__desc">{description}</div>}
      </div>

      {error && <div className="refill-page__error">{error}</div>}

      {state.can_add_group && (
        <div className="refill-page__actions">
          <button type="button" className="refill-page__add-group">
            {t('Add material group')}
          </button>
        </div>
      )}

      {groupCount === 0 ? (
        <div className="refill-page__empty">
          <img className="refill-page__empty-image" src={emptyRefillGroupImage} alt="" />
          <div className="refill-page__empty-label">{t('No auto-refill filament.')}</div>
        </div>
      ) : (
        <div className="refill-page__columns">
          {groups.map((group) => (
            <GroupColumn key={group.filament_id} group={group} editable={editable} />
          ))}
        </div>
      )}
    </div>
  );
}
