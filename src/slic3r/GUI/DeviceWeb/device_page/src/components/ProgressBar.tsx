
interface ProgressBarProps {
	percent: number;
	fg_color: string;
	bg_color?: string;
	rounded?: boolean;
	className?: string;
}

export default function ProgressBar({ percent, fg_color, bg_color="bg-gray-200", rounded=true, className }: ProgressBarProps) {

	return (
		< div className={`relative ${className} rounded-full ${bg_color}`} >
			<div className={`absolute h-full ${rounded ? "rounded-full" : "rounded-l-full"} ${fg_color}`} style={{ width: `${percent}%` }} />
		</div >
	);
};
