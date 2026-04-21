import { createRootRoute, Link, Outlet, useRouterState } from '@tanstack/react-router'

function RootComponent() {
  const pathname = useRouterState({ select: (s) => s.location.pathname })
  const hideNav = pathname === '/filament'

  return (
    <>
      {!hideNav && (
        <div className="flex gap-4 p-2 border-b border-neutral-200 dark:border-neutral-700 text-sm">
          <Link to="/" className="font-bold hover:text-green-500 [&.active]:text-green-500">Home</Link>
          <Link to="/i18n-demo" className="font-bold hover:text-green-500 [&.active]:text-green-500">i18n Demo</Link>
          <Link to="/about" className="font-bold hover:text-green-500 [&.active]:text-green-500">About</Link>
          <Link to="/app" className="font-bold hover:text-green-500 [&.active]:text-green-500">App</Link>
        </div>
      )}
      <Outlet />
    </>
  )
}

export const Route = createRootRoute({
  component: RootComponent,
})
