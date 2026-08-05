import { useLocation, Link } from 'react-router-dom';
import { useLingui } from '@lingui/react';
import { useRef, useEffect, useState } from 'preact/hooks';
import { useIsMobile } from '@/hooks/use-mobile';

const navigationItems = [
  { path: '/device-tool', key: 'sys.menu.device_tool' },
  { path: '/model-verification', key: 'sys.menu.model_verification' },
  { path: '/application-management', key: 'sys.menu.application_management' },
  { path: '/hardware-management', key: 'sys.menu.hardware_management' },
  { path: '/capture-settings', key: 'sys.menu.capture_settings' },
  { path: '/system-settings', key: 'sys.menu.system_settings' },
  { path: '/storage-management', key: 'sys.menu.storage_management' },
  { path: '/device-information', key: 'sys.menu.device_information' },
];

export default function Menu() {
  const location = useLocation();
  const { i18n } = useLingui();
  const scrollContainerRef = useRef<HTMLDivElement>(null);
  const [showLeftArrow, setShowLeftArrow] = useState(false);
  const [showRightArrow, setShowRightArrow] = useState(false);

  // Check scroll position and show/hide arrows
  const checkScrollPosition = () => {
    if (scrollContainerRef.current) {
      const { scrollLeft, scrollWidth, clientWidth } = scrollContainerRef.current;
      setShowLeftArrow(scrollLeft > 0);
      setShowRightArrow(scrollLeft < scrollWidth - clientWidth - 1);
    }
  };

  // Scroll to specified direction
  const scrollTo = (direction: 'left' | 'right') => {
    if (scrollContainerRef.current) {
      const scrollAmount = 200; // Scroll distance
      const currentScroll = scrollContainerRef.current.scrollLeft;
      const newScroll = direction === 'left' 
        ? currentScroll - scrollAmount 
        : currentScroll + scrollAmount;
      
      scrollContainerRef.current.scrollTo({
        left: newScroll,
        behavior: 'smooth'
      });
    }
  };

  // Listen to scroll events
  useEffect(() => {
    const scrollContainer = scrollContainerRef.current;
    if (scrollContainer) {
      scrollContainer.addEventListener('scroll', checkScrollPosition);
      checkScrollPosition(); // Initial check
      
      return () => {
        scrollContainer.removeEventListener('scroll', checkScrollPosition);
      };
    }
   return undefined;
  }, []);

  // Listen to window size changes
  useEffect(() => {
    const handleResize = () => {
      checkScrollPosition();
    };

    window.addEventListener('resize', handleResize);
    return () => window.removeEventListener('resize', handleResize);
  }, []);

  return (
    <nav className="bg-[#F7F8FA] flex justify-center items-center border-b border-[#E5E6EB] relative z-10">
      <div className="flex justify-center items-center  sm:px-6 lg:px-8 w-full relative">
        {/* Left scroll arrow - hidden on mobile */}
        {showLeftArrow && !useIsMobile() && (
          <button
            onClick={() => {
              scrollTo('left');
            }}
            className="absolute left-4 top-1/2 -translate-y-1/2 z-20 bg-white/80 hover:bg-white border border-gray-200 rounded-full p-1 shadow-sm transition-all duration-200 hover:shadow-md"
            aria-label="Scroll left"
          >
            <svg className="w-4 h-4 text-gray-600" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M15 19l-7-7 7-7" />
            </svg>
          </button>
        )}

        {/* Right scroll arrow - hidden on mobile */}
        {showRightArrow && !useIsMobile() && (
          <button
            onClick={() => {
              scrollTo('right');
            }}
            className="absolute right-4 top-1/2 -translate-y-1/2 z-20 bg-white/80 hover:bg-white border border-gray-200 rounded-full p-1 shadow-sm transition-all duration-200 hover:shadow-md"
            aria-label="Scroll right"
          >
            <svg className="w-4 h-4 text-gray-600" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M9 5l7 7-7 7" />
            </svg>
          </button>
        )}

        <div 
          ref={scrollContainerRef}
          className="flex space-x-8 mx-4 overflow-x-auto scrollbar-hide smooth-scroll px-2"
          style={{ scrollbarWidth: 'none', msOverflowStyle: 'none' }}
        >
          {navigationItems.map((item) => {
            const isActive = location.pathname === item.path
            return (
              <Link
                key={item.path}
                to={item.path}
                className={`whitespace-nowrap py-4 px-1 border-b-2 text-sm transition-colors flex-shrink-0 ${
                  isActive
                    ? 'font-bold border-primary border-b-[2px] text-primary '
                    : 'border-transparent text-text-secondary hover:text-primary hover:border-primary'
                }`}
              >
                {i18n._(item.key)}
              </Link>
            )
          })}
        </div>
      </div>
    </nav>
  );
}
