export interface Theme {
  id: string;
  name: string;
  description: string;
  category: 'dark' | 'light';
  colors: {
    bg: string;
    panel: string;
    elevated: string;
    border: string;
    text: string;
    muted: string;
    accent: string;
    accentInk: string;
    sidebar: string;
    input: string;
    // Status colors
    success: string;
    warning: string;
    error: string;
    info: string;
    // Chart colors
    chart1: string;
    chart2: string;
    chart3: string;
    chart4: string;
  };
}

export const themes: Theme[] = [
  {
    id: 'forest',
    name: 'Forest',
    description: 'Cool green workspace with natural tones',
    category: 'dark',
    colors: {
      bg: '#101819',
      panel: '#152022',
      elevated: '#1b2a2b',
      border: '#2b3a3b',
      text: '#e1e9e7',
      muted: '#91a6a5',
      accent: '#9bdfc6',
      accentInk: '#163a30',
      sidebar: '#111c1e',
      input: '#101b1d',
      success: '#9bdfc6',
      warning: '#f0c674',
      error: '#e8998d',
      info: '#81a1c1',
      chart1: '#9bdfc6',
      chart2: '#81a1c1',
      chart3: '#f0c674',
      chart4: '#9db7bb',
    },
  },
  {
    id: 'midnight',
    name: 'Midnight',
    description: 'Deep blue with silver accents',
    category: 'dark',
    colors: {
      bg: '#111622',
      panel: '#192130',
      elevated: '#202c40',
      border: '#303e52',
      text: '#e0e9f6',
      muted: '#9caec8',
      accent: '#9dc3fc',
      accentInk: '#182f51',
      sidebar: '#131b29',
      input: '#141d2c',
      success: '#a3be8c',
      warning: '#ebcb8b',
      error: '#bf616a',
      info: '#88c0d0',
      chart1: '#9dc3fc',
      chart2: '#88c0d0',
      chart3: '#ebcb8b',
      chart4: '#b48ead',
    },
  },
  {
    id: 'dracula',
    name: 'Dracula',
    description: 'Vibrant purple and cyan on dark background',
    category: 'dark',
    colors: {
      bg: '#282a36',
      panel: '#1e1f29',
      elevated: '#343746',
      border: '#44475a',
      text: '#f8f8f2',
      muted: '#6272a4',
      accent: '#bd93f9',
      accentInk: '#1e1f29',
      sidebar: '#21222c',
      input: '#1e1f29',
      success: '#50fa7b',
      warning: '#f1fa8c',
      error: '#ff5555',
      info: '#8be9fd',
      chart1: '#bd93f9',
      chart2: '#8be9fd',
      chart3: '#f1fa8c',
      chart4: '#ff79c6',
    },
  },
  {
    id: 'nord',
    name: 'Nord',
    description: 'Arctic-inspired blue palette',
    category: 'dark',
    colors: {
      bg: '#2e3440',
      panel: '#3b4252',
      elevated: '#434c5e',
      border: '#4c566a',
      text: '#eceff4',
      muted: '#d8dee9',
      accent: '#88c0d0',
      accentInk: '#2e3440',
      sidebar: '#2e3440',
      input: '#3b4252',
      success: '#a3be8c',
      warning: '#ebcb8b',
      error: '#bf616a',
      info: '#5e81ac',
      chart1: '#88c0d0',
      chart2: '#81a1c1',
      chart3: '#ebcb8b',
      chart4: '#b48ead',
    },
  },
  {
    id: 'tokyo-night',
    name: 'Tokyo Night',
    description: 'Modern Japanese-inspired dark theme',
    category: 'dark',
    colors: {
      bg: '#1a1b26',
      panel: '#16161e',
      elevated: '#24283b',
      border: '#414868',
      text: '#c0caf5',
      muted: '#a9b1d6',
      accent: '#7aa2f7',
      accentInk: '#1a1b26',
      sidebar: '#16161e',
      input: '#1a1b26',
      success: '#9ece6a',
      warning: '#e0af68',
      error: '#f7768e',
      info: '#7dcfff',
      chart1: '#7aa2f7',
      chart2: '#7dcfff',
      chart3: '#e0af68',
      chart4: '#bb9af7',
    },
  },
  {
    id: 'gruvbox',
    name: 'Gruvbox',
    description: 'Retro warm tones with excellent contrast',
    category: 'dark',
    colors: {
      bg: '#282828',
      panel: '#1d2021',
      elevated: '#3c3836',
      border: '#504945',
      text: '#ebdbb2',
      muted: '#a89984',
      accent: '#b8bb26',
      accentInk: '#1d2021',
      sidebar: '#1d2021',
      input: '#282828',
      success: '#b8bb26',
      warning: '#fabd2f',
      error: '#fb4934',
      info: '#83a598',
      chart1: '#b8bb26',
      chart2: '#83a598',
      chart3: '#fabd2f',
      chart4: '#d3869b',
    },
  },
  {
    id: 'monokai',
    name: 'Monokai Pro',
    description: 'Classic warm dark with vibrant highlights',
    category: 'dark',
    colors: {
      bg: '#2d2a2e',
      panel: '#221f22',
      elevated: '#403e41',
      border: '#5b595c',
      text: '#fcfcfa',
      muted: '#939293',
      accent: '#ffd866',
      accentInk: '#2d2a2e',
      sidebar: '#221f22',
      input: '#2d2a2e',
      success: '#a9dc76',
      warning: '#ffd866',
      error: '#ff6188',
      info: '#78dce8',
      chart1: '#ffd866',
      chart2: '#78dce8',
      chart3: '#fc9867',
      chart4: '#ab9df2',
    },
  },
  {
    id: 'catppuccin',
    name: 'Catppuccin',
    description: 'Soothing pastel dark theme',
    category: 'dark',
    colors: {
      bg: '#1e1e2e',
      panel: '#181825',
      elevated: '#313244',
      border: '#45475a',
      text: '#cdd6f4',
      muted: '#bac2de',
      accent: '#89b4fa',
      accentInk: '#1e1e2e',
      sidebar: '#181825',
      input: '#1e1e2e',
      success: '#a6e3a1',
      warning: '#f9e2af',
      error: '#f38ba8',
      info: '#89dceb',
      chart1: '#89b4fa',
      chart2: '#89dceb',
      chart3: '#f9e2af',
      chart4: '#cba6f7',
    },
  },
  {
    id: 'paper',
    name: 'Paper',
    description: 'Clean light theme for bright environments',
    category: 'light',
    colors: {
      bg: '#f0f2ec',
      panel: '#fafbf7',
      elevated: '#e4e9df',
      border: '#cad2c5',
      text: '#21332b',
      muted: '#586d60',
      accent: '#24694d',
      accentInk: '#ffffff',
      sidebar: '#e7ece3',
      input: '#ffffff',
      success: '#4a9d6f',
      warning: '#c6943e',
      error: '#c74e4e',
      info: '#4a7a9d',
      chart1: '#24694d',
      chart2: '#4a7a9d',
      chart3: '#c6943e',
      chart4: '#8e6d9d',
    },
  },
];

export function getTheme(id: string): Theme {
  return themes.find((t) => t.id === id) ?? themes[0]!;
}

export function applyTheme(theme: Theme): void {
  const root = document.documentElement;
  root.dataset.theme = theme.id;
  
  // Apply color variables
  Object.entries(theme.colors).forEach(([key, value]) => {
    const cssVar = `--${key.replace(/([A-Z])/g, '-$1').toLowerCase()}`;
    root.style.setProperty(cssVar, value);
  });
  
  // Set color scheme for browser UI
  root.style.colorScheme = theme.category;
}

// Typography scale (based on 1.25 ratio - major third)
export const typography = {
  fontFamily: {
    base: "'Inter Variable', 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif",
    mono: "'JetBrains Mono', 'Fira Code', 'SF Mono', 'Cascadia Code', Consolas, monospace",
  },
  fontSize: {
    xs: '0.688rem',    // 11px
    sm: '0.813rem',    // 13px
    base: '0.875rem',  // 14px
    md: '1rem',        // 16px
    lg: '1.125rem',    // 18px
    xl: '1.313rem',    // 21px
    '2xl': '1.563rem', // 25px
    '3xl': '2rem',     // 32px
    '4xl': '2.5rem',   // 40px
  },
  fontWeight: {
    normal: 400,
    medium: 500,
    semibold: 600,
    bold: 700,
  },
  lineHeight: {
    tight: 1.2,
    normal: 1.5,
    relaxed: 1.8,
  },
  letterSpacing: {
    tight: '-0.02em',
    normal: '0',
    wide: '0.05em',
    wider: '0.1em',
  },
};

// Spacing scale (4px base unit)
export const spacing = {
  0: '0',
  px: '1px',
  0.5: '0.125rem', // 2px
  1: '0.25rem',    // 4px
  1.5: '0.375rem', // 6px
  2: '0.5rem',     // 8px
  2.5: '0.625rem', // 10px
  3: '0.75rem',    // 12px
  4: '1rem',       // 16px
  5: '1.25rem',    // 20px
  6: '1.5rem',     // 24px
  7: '1.75rem',    // 28px
  8: '2rem',       // 32px
  10: '2.5rem',    // 40px
  12: '3rem',      // 48px
  16: '4rem',      // 64px
  20: '5rem',      // 80px
};

// Motion design tokens
export const motion = {
  duration: {
    fast: '120ms',
    normal: '200ms',
    slow: '300ms',
  },
  easing: {
    default: 'cubic-bezier(0.4, 0, 0.2, 1)',
    in: 'cubic-bezier(0.4, 0, 1, 1)',
    out: 'cubic-bezier(0, 0, 0.2, 1)',
    inOut: 'cubic-bezier(0.4, 0, 0.2, 1)',
  },
};

// Border radius scale
export const radius = {
  none: '0',
  sm: '0.25rem',   // 4px
  md: '0.375rem',  // 6px
  lg: '0.5rem',    // 8px
  xl: '0.75rem',   // 12px
  '2xl': '1rem',   // 16px
  full: '9999px',
};

// Shadow scale
export const shadows = {
  sm: '0 1px 2px 0 rgba(0, 0, 0, 0.05)',
  md: '0 4px 6px -1px rgba(0, 0, 0, 0.1), 0 2px 4px -1px rgba(0, 0, 0, 0.06)',
  lg: '0 10px 15px -3px rgba(0, 0, 0, 0.1), 0 4px 6px -2px rgba(0, 0, 0, 0.05)',
  xl: '0 20px 25px -5px rgba(0, 0, 0, 0.1), 0 10px 10px -5px rgba(0, 0, 0, 0.04)',
  inner: 'inset 0 2px 4px 0 rgba(0, 0, 0, 0.06)',
};
