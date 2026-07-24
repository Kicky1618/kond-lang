type IconProps = {
  className?: string;
};

export function ArrowUpRight({ className = "" }: IconProps) {
  return (
    <svg aria-hidden="true" className={className} fill="none" viewBox="0 0 16 16">
      <path d="M3 13 13 3M5 3h8v8" stroke="currentColor" strokeLinecap="round" strokeLinejoin="round" strokeWidth="1.4" />
    </svg>
  );
}

export function ChevronRight({ className = "" }: IconProps) {
  return (
    <svg aria-hidden="true" className={className} fill="none" viewBox="0 0 16 16">
      <path d="m6 3 5 5-5 5" stroke="currentColor" strokeLinecap="round" strokeLinejoin="round" strokeWidth="1.5" />
    </svg>
  );
}

export function SearchIcon({ className = "" }: IconProps) {
  return (
    <svg aria-hidden="true" className={className} fill="none" viewBox="0 0 16 16">
      <circle cx="7" cy="7" r="4.2" stroke="currentColor" strokeWidth="1.3" />
      <path d="m10.2 10.2 3 3" stroke="currentColor" strokeLinecap="round" strokeWidth="1.3" />
    </svg>
  );
}

export function CheckIcon({ className = "" }: IconProps) {
  return (
    <svg aria-hidden="true" className={className} fill="none" viewBox="0 0 16 16">
      <path d="m3.5 8.2 2.7 2.7 6.3-6.2" stroke="currentColor" strokeLinecap="round" strokeLinejoin="round" strokeWidth="1.5" />
    </svg>
  );
}

export function CopyIcon({ className = "" }: IconProps) {
  return (
    <svg aria-hidden="true" className={className} fill="none" viewBox="0 0 16 16">
      <rect height="8.5" rx="1.5" stroke="currentColor" strokeWidth="1.2" width="8.5" x="5" y="4.5" />
      <path d="M3.2 11.2H3A1.5 1.5 0 0 1 1.5 9.7V3A1.5 1.5 0 0 1 3 1.5h6.7A1.5 1.5 0 0 1 11.2 3v.2" stroke="currentColor" strokeLinecap="round" strokeWidth="1.2" />
    </svg>
  );
}
